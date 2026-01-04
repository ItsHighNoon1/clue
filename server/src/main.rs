/**
 * Please forgive me as I am new to Rust and I am trying to be
 * idiomatic instead of just throwing a bunch of unwraps everywhere
 */

use std::{io::BufRead, net::Ipv6Addr};

struct Card {
    id: i16,
    name: String,
}

struct Player<'a> {
    stream: std::net::TcpStream,
    address: std::net::SocketAddr,
    eliminated: bool,
    id: i8,
    name: String,
    hand: Vec<&'a Card>,
}

struct Lobby<'a> {
    thread: Option<std::thread::JoinHandle<()>>,
    players: Vec<Player<'a>>,
    solution: Vec<&'a Card>,
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
    let settings = read_settings(settings_path)?;
    
    // Continuously create lobbies
    let listener = match std::net::TcpListener::bind(std::net::SocketAddr::from((Ipv6Addr::UNSPECIFIED, settings.port))) {
        Ok(socket) => socket,
        Err(error) => return Err(error.to_string()),
    };
    match listener.set_nonblocking(true) {
        Ok(_) => (),
        Err(error) => return Err(error.to_string()),
    }
    let mut lobbies: Vec<Lobby> = Vec::new();
    loop {
        // Clean up finished lobbies
        while let Some(index) = lobbies.iter().position(|lobby| match &lobby.thread {
            Some(thread) => thread.is_finished(),
            None => false, // Have not started this lobby
        }) {
            let finished_lobby = lobbies.remove(index);
            let _ = finished_lobby.thread.unwrap().join();
            println!("Cleaned up a lobby");
        }

        // Try to create a new lobby
        println!("Creating new lobby {}", listener.local_addr().unwrap());
        let mut lobby = Lobby {
            thread: None,
            players: Vec::new(),
            solution: Vec::new(),
        };
        let mut player_index = 0;
        let mut timeout_start: Option<std::time::Instant> = None;
        while lobby.players.len() < settings.lobby_size.try_into().unwrap() {
            // Wait for players
            match listener.accept() {
                Ok(connection) => {
                    println!("got a connection: {}", connection.1);
                    lobby.players.push(Player {
                        stream: connection.0,
                        address: connection.1,
                        eliminated: false,
                        id: player_index,
                        name: String::from("Test"),
                        hand: Vec::new(),
                    });
                    player_index += 1;
                }
                Err(_) => {
                    std::thread::sleep(std::time::Duration::new(1, 0));
                    continue;
                }
            }

            if lobby.players.len() >= settings.lobby_size.try_into().unwrap() {
                // Lobby full, begin
                break;
            }

            // If there are more than 2 players, start the game start timer
            if lobby.players.len() >= 2 {
                println!("2 players joined, start timeout timer");
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

        println!("Starting lobby");
        lobbies.push(lobby);
        run_lobby(lobbies.last_mut().unwrap(), &settings);
    }
}

fn run_lobby(lobby: &mut Lobby, settings: &Settings) {
    lobby.thread = Some(std::thread::spawn(move || {

    }));
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

