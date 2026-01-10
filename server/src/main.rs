/**
 * Please forgive me as I am new to Rust and I am trying to be
 * idiomatic instead of just throwing a bunch of unwraps everywhere
 */

mod frames;

use std::io::{BufRead, Write};
use rand::seq::SliceRandom;
use rand::Rng;
use frames::Card;

struct Player {
    in_stream: std::io::BufReader<std::net::TcpStream>,
    out_stream: std::io::BufWriter<std::net::TcpStream>,
    address: std::net::SocketAddr,
    eliminated: bool,
    autoplay: bool,
    id: i8,
    name: String,
    hand: Vec<Card>,
}

struct Lobby {
    players: Vec<Player>,
    solution: Vec<Card>,
}

struct Settings {
    port: u16,
    timeout: i8,
    lobby_size: i8,
    lobby_wait: i8,
    cards: Vec<Vec<Card>>,
}

fn main() -> Result<(), String> {
    // Read settings.txt
    let args: Vec<String> = std::env::args().collect();
    let settings_path = match args.get(1) {
        Some(string) => string,
        None => return Err(String::from("Usage: ./server [settings file]")),
    };
    let settings = std::sync::Arc::new(read_settings(settings_path)?);

    // Create the rules frame which we will send to everyone when they join
    let mut rules_frame = frames::RulesFrame {
        player_id: 0,
        cards: settings.cards.to_vec(),
    };
    
    // Continuously create lobbies
    let listener = match std::net::TcpListener::bind(std::net::SocketAddr::from((std::net::Ipv6Addr::UNSPECIFIED, settings.port))) {
        Ok(socket) => socket,
        Err(error) => return Err(error.to_string()),
    };
    match listener.set_nonblocking(true) {
        Ok(_) => (),
        Err(error) => return Err(error.to_string()),
    }

    loop {
        // Try to create a new lobby
        println!("Creating new lobby {}", listener.local_addr().unwrap());
        rules_frame.player_id = 0;
        let mut lobby = Lobby {
            players: Vec::new(),
            solution: Vec::new(),
        };
        let mut timeout_start: Option<std::time::Instant> = None;
        while lobby.players.len() < settings.lobby_size.try_into().unwrap() {
            // Wait for players
            match listener.accept() {
                Ok(connection) => {
                    if settings.timeout == 0 {
                        if let Err(error) = connection.0.set_read_timeout(None) {
                            println!("Failed to set timeout: {}", error.to_string());
                        }
                    } else {
                        if let Err(error) = connection.0.set_read_timeout(Some(std::time::Duration::new(settings.timeout as u64, 0))) {
                            println!("Failed to set timeout: {}", error.to_string());
                        }
                    }
                    match connection.0.try_clone() {
                        Ok(cloned_stream) => {
                            let mut in_stream = std::io::BufReader::new(connection.0);
                            let mut out_stream = std::io::BufWriter::new(cloned_stream);
                            match frames::expect_frame::<frames::ConnectFrame>(&mut in_stream) {
                                Ok(connect_frame) => {
                                    if frames::send_frame(&mut out_stream, &rules_frame).is_ok() {
                                        let _ = out_stream.flush();
                                        println!("{} connected", connect_frame.name);
                                        rules_frame.player_id += 1;
                                        lobby.players.push(Player {
                                            in_stream: in_stream,
                                            out_stream: out_stream,
                                            address: connection.1,
                                            eliminated: false,
                                            autoplay: false,
                                            id: rules_frame.player_id,
                                            name: connect_frame.name,
                                            hand: Vec::new(),
                                        });
                                    } else {
                                        // Failed to send for some reason, maybe they closed the socket?
                                    }
                                }
                                Err(error) => {
                                    let _ = frames::send_frame(&mut out_stream, &frames::DebugFrame { message: error.to_string() });
                                }
                            }
                        }
                        Err(_) => {
                            eprintln!("Failed to clone stream for {}", connection.1);
                            continue;
                        }
                    }
                }
                Err(_) => {
                    std::thread::sleep(std::time::Duration::new(1, 0));
                }
            }

            if lobby.players.len() >= settings.lobby_size.try_into().unwrap() {
                // Lobby full, begin
                break;
            }

            // If there are more than 2 players, start the game start timer (if we want one)
            if lobby.players.len() >= 2 && settings.lobby_wait > 0 && timeout_start.is_none() {
                println!("Started lobby timeout");
                timeout_start = Some(std::time::Instant::now());
            }
            match timeout_start {
                None => (),
                Some(time) => {
                    if time.elapsed().as_secs() > settings.lobby_wait.try_into().unwrap() {
                        break;
                    }
                }
            }
        }

        // Create a thread to handle this lobby so the main thread can work on another
        let thread_settings = settings.clone();
        std::thread::spawn(move || {
            run_lobby(&mut lobby, thread_settings.as_ref());
        });
    }
}

fn run_lobby(lobby: &mut Lobby, settings: &Settings) {
    // Choose solution and shuffle deck
    lobby.players.shuffle(&mut rand::rng());
    let mut deck = Vec::new();
    for category in &settings.cards {
        let mut cards_in_category = category.to_vec();
        let solution_card_idx = rand::rng().random_range(0..cards_in_category.len());
        lobby.solution.push(cards_in_category.remove(solution_card_idx));
        deck.append(&mut cards_in_category);
    }
    deck.shuffle(&mut rand::rng());

    // Deal cards to players
    let mut turn_idx = 0;
    while deck.len() > 0 {
        lobby.players.get_mut(turn_idx).unwrap().hand.push(deck.pop().unwrap());
        turn_idx = (turn_idx + 1) % lobby.players.len();
    }

    // Send game start frames
    let mut players_info = Vec::new();
    for player in lobby.players.iter_mut() {
        player.hand.sort_by_key(|card| card.id);
        players_info.push(frames::Player {
            id: player.id,
            name: player.name.clone(),
            hand_size: player.hand.len() as i16,
        });
    }
    for player in lobby.players.iter_mut() {
        let mut hand_ids = Vec::new();
        for card in player.hand.iter() {
            hand_ids.push(card.id);
        }
        let start_frame = frames::StartFrame {
            hand: hand_ids,
            players: players_info.clone(),
        };
        if frames::send_frame(&mut player.out_stream, &start_frame).is_err() {
            let cancel_game = frames::DebugFrame {
                message: String::from("Aborting game due to early disconnect"),
            };
            for player in lobby.players.iter_mut() {
                let _ = frames::send_frame(&mut player.out_stream, &cancel_game);
            }
            return;
        }
    }

    println!("got here");
    let test = frames::DebugFrame {
        message: String::from("TEST"),
    };
    frames::send_frame(&mut lobby.players.get_mut(0).unwrap().out_stream, &test);

    println!("shutting down lobby");
}

fn read_settings(path: &String) -> Result<Settings, String> {
    enum SettingsFileState {
        Port,
        Timeout,
        LobbySize,
        LobbyWait,
        Cards,
    }

    // Default settings
    let mut settings = Settings {
        port: 49422,
        timeout: 3,
        lobby_size: 4,
        lobby_wait: 10,
        cards: Vec::new(),
    };

    let file = match std::fs::File::open(path) {
        Ok(file) => file,
        Err(error) => return Err(error.to_string()),
    };
    let reader = std::io::BufReader::new(file);
    let mut state = SettingsFileState::Port;
    let mut card_id = 0;

    // Read lines
    for line_ in reader.lines() {
        let line = line_.unwrap();
        if line.is_empty() {
            if !matches!(state, SettingsFileState::Port) {
                state = SettingsFileState::Cards;
                settings.cards.push(Vec::new());
            }
            continue;
        }
        if line.starts_with("#") || line.starts_with("//") {
            continue;
        }
        match state {
            SettingsFileState::Port => {
                settings.port = match str::parse(line.as_str()) {
                    Ok(int) => int,
                    Err(error) => return Err(error.to_string()),
                };
                state = SettingsFileState::Timeout;
            }
            SettingsFileState::Timeout => {
                settings.timeout = match str::parse(line.as_str()) {
                    Ok(int) => int,
                    Err(error) => return Err(error.to_string()),
                };
                state = SettingsFileState::LobbySize;
            }
            SettingsFileState::LobbySize => {
                settings.lobby_size = match str::parse(line.as_str()) {
                    Ok(int) => int,
                    Err(error) => return Err(error.to_string()),
                };
                state = SettingsFileState::LobbyWait;
            }
            SettingsFileState::LobbyWait => {
                settings.lobby_wait = match str::parse(line.as_str()) {
                    Ok(int) => int,
                    Err(error) => return Err(error.to_string()),
                };
                state = SettingsFileState::Cards;
            }
            SettingsFileState::Cards => {
                // Found a card
                let card = Card {
                    id: card_id,
                    name: line,
                };
                card_id += 1;
                settings.cards.last_mut().unwrap().push(card);
            }
        }
    }

    // Validate parameters
    if settings.lobby_size < 2 {
        return Err(String::from("Lobby size must be at least 2"));
    }
    if settings.lobby_wait < 0 {
        return Err(String::from("Lobby wait time cannot be negative"));
    }
    if settings.timeout < 0 {
        return Err(String::from("Turn timeout cannot be negative"));
    }

    // Prune any empty categories caused by extra newlines
    while let Some(index) = settings.cards.iter().position(|category| category.is_empty()) {
        settings.cards.remove(index);
    }
    
    return Ok(settings);
}

