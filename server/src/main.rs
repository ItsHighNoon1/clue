/**
 * Please forgive me as I am new to Rust and I am trying to be
 * idiomatic instead of just throwing a bunch of unwraps everywhere
 */

mod frames;

use std::io::{BufRead, Write};
use rand::seq::{IndexedRandom, SliceRandom};
use rand::Rng;
use frames::Card;

struct Player {
    stream: std::net::TcpStream,
    address: std::net::SocketAddr,
    eliminated: bool,
    autoplay: bool,
    id: i8,
    name: String,
    hand: Vec<Card>,
}
impl Player {
    fn to_json(&self) -> String {
        return format!("{{\"ip\":\"{}\",\"name\":\"{}\"}}", self.address.ip().to_string(), self.name);
    }
}
impl Clone for Player {
    fn clone(&self) -> Self {
        return Player {
            stream: self.stream.try_clone().unwrap(), // TODO
            address: self.address.clone(),
            eliminated: self.eliminated,
            autoplay: self.autoplay,
            id: self.id,
            name: self.name.clone(),
            hand: self.hand.clone(),
        }
    }
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
    endpoint: String,
    password: String,
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
        rules_frame.player_id = 0;
        let mut lobby = Lobby {
            players: Vec::new(),
            solution: Vec::new(),
        };
        let mut timeout_start: Option<std::time::Instant> = None;
        while lobby.players.len() < settings.lobby_size.try_into().unwrap() {
            // Wait for players
            match listener.accept() {
                Ok(mut connection) => {
                    if settings.timeout == 0 {
                        if let Err(error) = connection.0.set_read_timeout(None) {
                            println!("Failed to set timeout: {}", error.to_string());
                        }
                    } else {
                        if let Err(error) = connection.0.set_read_timeout(Some(std::time::Duration::new(settings.timeout as u64, 0))) {
                            println!("Failed to set timeout: {}", error.to_string());
                        }
                    }
                    match frames::expect_frame::<frames::ConnectFrame>(&mut connection.0) {
                        Ok(connect_frame) => {
                            if frames::send_frame(&mut connection.0, &rules_frame).is_ok() {
                                let _ = connection.0.flush();
                                println!("{} connected", connect_frame.name);
                                lobby.players.push(Player {
                                    stream: connection.0,
                                    address: connection.1,
                                    eliminated: false,
                                    autoplay: false,
                                    id: rules_frame.player_id,
                                    name: connect_frame.name,
                                    hand: Vec::new(),
                                });
                                rules_frame.player_id += 1;
                            } else {
                                // Failed to send for some reason, maybe they closed the socket?
                            }
                        }
                        Err(error) => {
                            let _ = frames::send_frame(&mut connection.0, &frames::DebugFrame { message: error.to_string() });
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
        println!("Creating lobby with above players");
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
    let mut turn_idx: i32 = 0;
    while deck.len() > 0 {
        lobby.players.get_mut(turn_idx as usize).unwrap().hand.push(deck.pop().unwrap());
        turn_idx = (turn_idx + 1) % lobby.players.len() as i32;
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
        if frames::send_frame(&mut player.stream, &start_frame).is_err() {
            let cancel_game = frames::DebugFrame {
                message: String::from("Aborting game due to early disconnect"),
            };
            for player in lobby.players.iter_mut() {
                let _ = frames::send_frame(&mut player.stream, &cancel_game);
            }
            return;
        }
    }

    // Run the game
    let winning_player;
    let mut first_turn = true;
    turn_idx = 0;
    loop {
        // Count eliminated players
        let mut eliminated_count = 0;
        let mut not_eliminated = 0;
        for player in lobby.players.iter() {
            if player.eliminated || player.autoplay {
                eliminated_count += 1;
            } else {
                not_eliminated = player.id;
            }
        }
        if eliminated_count >= lobby.players.len() - 1 {
            // Game ends due to players eliminated
            let game_end_frame = frames::GameEndFrame {
                winner: not_eliminated,
                won_by_default: true,
            };
            for player in lobby.players.iter_mut() {
                let _ = frames::send_frame(&mut player.stream, &game_end_frame);
            }
            winning_player = Some(lobby.players.iter().find(|player| player.id == not_eliminated).unwrap().clone());
            break;
        }

        // Advance player turn and announce it
        if lobby.players.len() == 0 {
            println!("Somehow a lobby had 0 players, emergency stopping");
            return;
        }
        if !first_turn {
            turn_idx = (turn_idx + 1) % lobby.players.len() as i32;
        } else {
            first_turn = false;
        }
        let mut current_player = lobby.players.get_mut(turn_idx as usize).unwrap().clone();
        
        // If this player is eliminated, skip their turn
        if current_player.eliminated || current_player.autoplay {
            continue;
        }

        // Notify everyone it is this player's turn
        let turn_start_frame = frames::TurnFrame {
            player_id: current_player.id,
        };
        for player in lobby.players.iter_mut() {
            let _ = frames::send_frame(&mut player.stream, &turn_start_frame);
        }

        // Expect that player to take an action
        let mut is_solving = false;
        let mut suggestion = Vec::new();
        if !current_player.autoplay {
            match frames::expect_frame::<frames::ActionFrame>(&mut current_player.stream) {
                Ok(action_frame) => {
                    is_solving = action_frame.is_solving;
                    for card in action_frame.suggestion.iter() {
                        suggestion.push(*card);
                    }
                },
                Err(_) => {
                    // Probably timed out, automate
                    let autoplay_reason_frame = frames::DebugFrame {
                        message: String::from("Set to autoplay due to timeout on turn"),
                    };
                    let _ = frames::send_frame(&mut current_player.stream, &autoplay_reason_frame);
                    lobby.players.get_mut(turn_idx as usize).unwrap().autoplay = true;
                    current_player.autoplay = true;
                }
            }
        }

        // Is the action valid?
        suggestion.sort();
        if suggestion.len() == settings.cards.len() {
            for category_idx in 0..settings.cards.len() {
                let mut is_found = false;
                for card in settings.cards.get(category_idx).unwrap() {
                    if card.id == suggestion[category_idx] {
                        is_found = true;
                        break;
                    }
                }
                if !is_found {
                    // Not one card per category
                    let autoplay_reason_frame = frames::DebugFrame {
                        message: String::from("Set to autoplay due to invalid set of suggested cards"),
                    };
                    let _ = frames::send_frame(&mut current_player.stream, &autoplay_reason_frame);
                    lobby.players.get_mut(turn_idx as usize).unwrap().autoplay = true;
                    current_player.autoplay = true;
                    break;
                }
            }
        } else {
            // Suggested the wrong number of cards
            let autoplay_reason_frame = frames::DebugFrame {
                message: String::from("Set to autoplay due to wrong number of suggested cards"),
            };
            let _ = frames::send_frame(&mut current_player.stream, &autoplay_reason_frame);
            lobby.players.get_mut(turn_idx as usize).unwrap().autoplay = true;
            current_player.autoplay = true;
        }

        // If the player is on autoplay, do a random suggestion
        if current_player.autoplay {
            is_solving = false;
            suggestion.clear();
            for category in settings.cards.iter() {
                suggestion.push(category.choose(&mut rand::rng()).unwrap().id);
            }
        }

        // If the player attempted to solve, handle those cases
        if is_solving {
            let mut solution_correct = true;
            for card in lobby.solution.iter() {
                if !suggestion.contains(&card.id) {
                    solution_correct = false;
                    break;
                }
            }
            if solution_correct {
                // The player won the game, send the game end frame
                let game_end_frame = frames::GameEndFrame {
                    winner: current_player.id,
                    won_by_default: false,
                };
                for player in lobby.players.iter_mut() {
                    let _ = frames::send_frame(&mut player.stream, &game_end_frame);
                }
                winning_player = Some(current_player);
                break;
            } else {
                // The player is eliminated
                lobby.players.get_mut(turn_idx as usize).unwrap().eliminated = true;
                current_player.eliminated = true;
                continue;
            }
        }

        // Program flow only gets here on a normal suggestion
        let mut queried_player_idx = (turn_idx + 1) % lobby.players.len() as i32;
        while queried_player_idx != turn_idx {
            // Tell everyone that someone is under the gun
            let mut queried_player = lobby.players.get_mut(queried_player_idx as usize).unwrap().clone();
            let query_frame = frames::ActionFrame {
                player_id: current_player.id,
                responder_id: queried_player.id,
                suggestion: suggestion.clone(),
                is_solving: is_solving,
            };
            for player in lobby.players.iter_mut() {
                let _ = frames::send_frame(&mut player.stream, &query_frame);
            }

            // Queried player needs to respond
            let mut player_chosen_card = -1;
            if !queried_player.autoplay {
                if let Ok(reply_frame) = frames::expect_frame::<frames::ReplyFrame>(&mut queried_player.stream) {
                    player_chosen_card = reply_frame.card;
                } else {
                    // Probably timed out, automate
                    let autoplay_reason_frame = frames::DebugFrame {
                        message: String::from("Set to autoplay due to timeout on query"),
                    };
                    let _ = frames::send_frame(&mut queried_player.stream, &autoplay_reason_frame);
                    lobby.players.get_mut(turn_idx as usize).unwrap().autoplay = true;
                    queried_player.autoplay = true;
                }
            }

            // First question... was the player obligated to respond?
            let mut response_card = None;
            for card in queried_player.hand.iter() {
                if suggestion.contains(&card.id) {
                    response_card = Some(card.id);
                    break;
                }
            }
            if response_card.is_some() {
                // Player is obligated to respond, did they respond legally?
                if !suggestion.contains(&player_chosen_card) {
                    // Responded with a card that wasn't suggested
                    let autoplay_reason_frame = frames::DebugFrame {
                        message: String::from("Set to autoplay due to not responding to a suggestion"),
                    };
                    let _ = frames::send_frame(&mut queried_player.stream, &autoplay_reason_frame);
                    queried_player.autoplay = true;
                } else {
                    let mut player_holds_card = false;
                    for card in queried_player.hand.iter() {
                        if card.id == player_chosen_card {
                            player_holds_card = true;
                            break;
                        }
                    }
                    if !player_holds_card {
                        // Responded with a card they don't hold
                        let autoplay_reason_frame = frames::DebugFrame {
                            message: String::from("Set to autoplay due to responding to a suggestion with a not held card"),
                        };
                        let _ = frames::send_frame(&mut queried_player.stream, &autoplay_reason_frame);
                        lobby.players.get_mut(turn_idx as usize).unwrap().autoplay = true;
                        queried_player.autoplay = true;
                    }

                    // Legal response
                    response_card = Some(player_chosen_card);
                }
            }

            // Send the reply to all players
            let reply_frame_real = frames::ReplyFrame {
                player_id: queried_player.id,
                card: response_card.unwrap_or(-1),
            };
            let reply_frame_broadcast = frames::ReplyFrame {
                player_id: queried_player.id,
                card: if response_card.is_some() { 0 } else { -1 },
            };
            for player in lobby.players.iter_mut() {
                if player.id == current_player.id || player.id == queried_player.id {
                    let _ = frames::send_frame(&mut player.stream, &reply_frame_real);
                } else {
                    let _ = frames::send_frame(&mut player.stream, &reply_frame_broadcast);
                }
            }

            if response_card.is_some() {
                break;
            }

            // Go next
            queried_player_idx = (queried_player_idx + 1) % lobby.players.len() as i32;
        }
    }

    // Wrap up the lobby
    let _ = wrap_up_lobby(lobby, settings, winning_player);
}

fn wrap_up_lobby(lobby: &mut Lobby, settings: &Settings, winning_player: Option<Player>) {
    // Shut down streams if they are still open
    for player in lobby.players.iter_mut() {
        let _ = player.stream.shutdown(std::net::Shutdown::Both);
    }

    if let Some(winner) = &winning_player {
        println!("Lobby ended with {} winning", winner.name);
    } else {
        println!("Lobby ended with no winner");
    }

    if settings.endpoint.is_empty() {
        // If no endpoint, we are done
        return;
    }

    // Else, report the results of this game to the leaderboard
    let client = reqwest::blocking::Client::new();
    let losing_player_jsons: Vec<String> = lobby.players.iter().filter_map(|player| {
        if let Some(winner) = winning_player.as_ref() {
            if winner.id == player.id {
                return None;
            } else {
                return Some(player.to_json());
            }
        } else {
            return Some(player.to_json());
        }
    }).collect();
    let losing_player_json_array = format!("[{}]", losing_player_jsons.join(","));
    let mut request_body = std::collections::HashMap::new();
    if let Some(winner) = winning_player {
        request_body.insert("winner", winner.to_json());
    }
    request_body.insert("losers", losing_player_json_array);
    match client.post(&settings.endpoint).json(&request_body).header("Authorization", &settings.password).send() {
        Ok(res) => {
            if res.status().is_success() {
                println!("Successfully saved lobby result");
            } else {
                println!("HTTP {}: {}", res.status().to_string(), res.text().unwrap_or(String::from("<empty>")));
            }
        }
        Err(err) => {
            println!("HTTP error submitting scores: {}", err.to_string());
        }
    }
}

fn read_settings(path: &String) -> Result<Settings, String> {
    enum SettingsFileState {
        Port,
        Timeout,
        LobbySize,
        LobbyWait,
        Endpoint,
        Password,
        Cards,
    }

    // Default settings
    let mut settings = Settings {
        port: 49422,
        timeout: 3,
        lobby_size: 4,
        lobby_wait: 10,
        endpoint: String::from(""),
        password: String::from(""),
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
                state = SettingsFileState::Endpoint;
            }
            SettingsFileState::Endpoint => {
                settings.endpoint = String::from(line.as_str());
                state = SettingsFileState::Password;
            }
            SettingsFileState::Password => {
                settings.password = String::from(line.as_str());
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

