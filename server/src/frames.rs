use std::io::{Read, Write};

// See frames.h
pub trait Frame<F> {
    fn serialize(&self) -> Vec<u8>;
    fn deserialize(bytes: &[u8]) -> Result<F, std::io::Error>;
    fn get_type() -> i8; // Is there a better way?
}

pub struct Card {
    pub id: i16,
    pub name: String,
}
impl Clone for Card {
    fn clone(&self) -> Self {
        return Card {
            id: self.id,
            name: self.name.clone(),
        }
    }
}

pub struct Player {
    pub id: i8,
    pub name: String,
    pub hand_size: i16,
}
impl Clone for Player {
    fn clone(&self) -> Self {
        return Player {
            id: self.id,
            name: self.name.clone(),
            hand_size: self.hand_size,
        }
    }
}

const FRAME_TYPE_UNKNOWN: i8 = 0;
pub struct FrameHeader {
    pub frame_type: i8,
    pub data_length: i32,
}
impl Frame<FrameHeader> for FrameHeader {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.frame_type as u8);
        bytes.push(0);
        bytes.push(0);
        bytes.push(0);
        let data_length_big_endian = self.data_length.to_be_bytes();
        bytes.push(data_length_big_endian[0]);
        bytes.push(data_length_big_endian[1]);
        bytes.push(data_length_big_endian[2]);
        bytes.push(data_length_big_endian[3]);
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<FrameHeader, std::io::Error> {
        if bytes.len() < 8 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete frame header"));
        }
        let frame_type = bytes[0] as u8;
        let data_length_big_endian: [u8; 4] = [bytes[4], bytes[5], bytes[6], bytes[7]];
        return Ok(FrameHeader {
            frame_type: frame_type as i8,
            data_length: i32::from_be_bytes(data_length_big_endian),
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_UNKNOWN;
    }
}

const FRAME_TYPE_DEBUG: i8 = 1;
pub struct DebugFrame {
    pub message: String,
}
impl Frame<DebugFrame> for DebugFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        let message_length_big_endian = (self.message.len() as i32).to_be_bytes();
        bytes.push(message_length_big_endian[0]);
        bytes.push(message_length_big_endian[1]);
        bytes.push(message_length_big_endian[2]);
        bytes.push(message_length_big_endian[3]);
        for byte in self.message.as_bytes() {
            bytes.push(*byte);
        }
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<DebugFrame, std::io::Error> {
        if bytes.len() < 4 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Incomplete debug frame"));
        }
        let message_length_big_endian: [u8; 4] = [bytes[0], bytes[1], bytes[2], bytes[3]];
        let message_length = i32::from_be_bytes(message_length_big_endian);
        if bytes.len() - 4 != message_length as usize {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Bad debug message length"));
        }
        let message = String::from_utf8(bytes[4..].to_vec());
        return match message {
            Ok(text) => Ok(DebugFrame { message: text }),
            Err(_) => Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Failed to decode debug message")),
        };
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_DEBUG;
    }
}

const FRAME_TYPE_CONNECT: i8 = 2;
pub struct ConnectFrame {
    pub name: String,
}
impl Frame<ConnectFrame> for ConnectFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        let mut cut_len = self.name.len();
        if cut_len > 127 {
            cut_len = 127;
        }
        bytes.push(cut_len as u8);
        for byte in self.name.as_bytes() {
            cut_len -= 1;
            bytes.push(*byte);
            if cut_len == 0 {
                break;
            }
        }
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<ConnectFrame, std::io::Error> {
        if bytes.len() < 1 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Incomplete connect frame"));
        }
        let name_length = bytes[0];
        if bytes.len() - 1 != name_length as usize {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Bad connect name length"));
        }
        let name = String::from_utf8(bytes[1..].to_vec());
        return match name {
            Ok(text) => Ok(ConnectFrame { name: text }),
            Err(_) => Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Failed to decode player name")),
        };
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_CONNECT;
    }
}

const FRAME_TYPE_RULES: i8 = 3;
pub struct RulesFrame {
    pub player_id: i8,
    pub cards: Vec<Vec<Card>>,
}
impl Frame<RulesFrame> for RulesFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut total_cards: i16 = 0;
        for category in self.cards.iter() {
            total_cards += category.len() as i16;
        }
        let mut bytes = Vec::new();
        bytes.push(self.player_id as u8);
        bytes.push(self.cards.len() as u8);
        let total_cards_big_endian = total_cards.to_be_bytes();
        bytes.push(total_cards_big_endian[0]);
        bytes.push(total_cards_big_endian[1]);
        for category in self.cards.iter() {
            let category_size_big_endian = (category.len() as i16).to_be_bytes();
            bytes.push(category_size_big_endian[0]);
            bytes.push(category_size_big_endian[1]);
        }
        for category in self.cards.iter() {
            for card in category {
                bytes.push(card.name.len() as u8);
                for c in card.name.bytes() {
                    bytes.push(c);
                }
            }
        }
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<RulesFrame, std::io::Error> {
        if bytes.len() < 4 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete rules frame"));
        }
        let player_id = bytes[0] as i8;
        if player_id < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Player ID too large"));
        }

        // Category info
        let num_categories = bytes[1] as i8;
        if num_categories < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Number of categories too large"));
        }
        let total_cards_big_endian: [u8; 2] = [bytes[2], bytes[3]];
        let total_cards = i16::from_be_bytes(total_cards_big_endian);
        if total_cards < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Number of cards too large"));
        }
        if bytes.len() < 4 + num_categories as usize * 2 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Rules frame does not contain category lengths"));
        }
        let mut category_sizes = Vec::new();
        let mut discovered_total_cards = 0;
        for category_idx in 0..num_categories {
            let category_size_big_endian: [u8; 2] = [bytes[4 + category_idx as usize * 2], bytes[4 + category_idx as usize * 2 + 1]];
            let category_size = i16::from_be_bytes(category_size_big_endian);
            if category_size < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Category too large"));
            }
            category_sizes.push(category_size);
            discovered_total_cards += category_size;
        }
        if discovered_total_cards != total_cards {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Sum of category sizes differs from total cards"));
        }

        // Card info
        let mut card_base_offset = 4 + num_categories as usize * 2;
        let mut card_idx = 0;
        let mut cards = Vec::new();
        for category_idx in 0..num_categories {
            let mut category = Vec::new();
            for _ in 0..*category_sizes.get(category_idx as usize).unwrap() {
                if bytes.len() < card_base_offset + 1 {
                    return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Rules frame does not contain card name length"));
                }
                let card_name_len = bytes[card_base_offset] as i8;
                if card_name_len < 0 {
                    return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Card name too long"));
                }
                if bytes.len() < card_base_offset + 1 + card_name_len as usize {
                    return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Rules frame does not contain card name"));
                }
                if let Ok(card_name) = String::from_utf8(bytes[card_base_offset + 1..card_base_offset + 1 + card_name_len as usize].to_vec()) {
                    category.push(Card {
                        id: card_idx,
                        name: card_name,
                    });
                    card_idx += 1;
                } else {
                    return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Failed to decode card name"));
                }
                card_base_offset += 1 + card_name_len as usize;
            }
            cards.push(category);
        }
        return Ok(RulesFrame { 
            player_id: player_id,
            cards: cards,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_RULES;
    }
}

const FRAME_TYPE_START: i8 = 4;
pub struct StartFrame {
    pub hand: Vec<i16>,
    pub players: Vec<Player>,
}
impl Frame<StartFrame> for StartFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        let mut sorted_players = self.players.clone();
        sorted_players.sort_by_key(|player| player.id);
        let your_hand_big_endian = (self.hand.len() as i16).to_be_bytes();
        bytes.push(your_hand_big_endian[0]);
        bytes.push(your_hand_big_endian[1]);
        bytes.push(self.players.len() as u8);
        bytes.push(0);
        for card in self.hand.iter() {
            let card_big_endian = (*card).to_be_bytes();
            bytes.push(card_big_endian[0]);
            bytes.push(card_big_endian[1]);
        }
        for player in self.players.iter() {
            bytes.push(player.id as u8);
        }
        for player in sorted_players.iter() {
            let hand_size_big_endian = player.hand_size.to_be_bytes();
            bytes.push(hand_size_big_endian[0]);
            bytes.push(hand_size_big_endian[1]);
        }
        for player in sorted_players.iter() {
            bytes.push(player.name.len() as u8);
            for c in player.name.as_bytes() {
                bytes.push(*c);
            }
        }
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<StartFrame, std::io::Error> {
        if bytes.len() < 4 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete start frame"));
        }
        let your_hand_big_endian: [u8; 2] = [bytes[0], bytes[1]];
        let your_hand = i16::from_be_bytes(your_hand_big_endian);
        if your_hand < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Hand size too large"));
        }
        let num_players = bytes[2] as i8;
        if num_players < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Player count too large"));
        }
        let mut section_base = 4;

        // Cards
        if bytes.len() < section_base + your_hand as usize * 2 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Start frame does not contain hand"));
        }
        let mut cards = Vec::new();
        for card_idx in 0..your_hand {
            let card_big_endian: [u8; 2] = [bytes[section_base + (card_idx as usize) * 2], bytes[section_base + (card_idx as usize) * 2 + 1]];
            let card = i16::from_be_bytes(card_big_endian);
            if card < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Card ID out of bounds"));
            }
            cards.push(card);
        }
        section_base += your_hand as usize * 2;

        // Player order and hand sizes
        if bytes.len() < section_base + num_players as usize * 3 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Start frame does not contain player order or hand sizes"));
        }
        let mut player_order = Vec::new();
        for player_idx in 0..num_players {
            player_order.push(bytes[section_base + player_idx as usize]);
        }
        section_base += num_players as usize;
        let mut player_hand_sizes = Vec::new();
        for player_idx in 0..num_players {
            let hand_size_big_endian: [u8; 2] = [bytes[section_base + (player_idx as usize) * 2], bytes[section_base + (player_idx as usize) * 2 + 1]];
            let hand_size = i16::from_be_bytes(hand_size_big_endian);
            if hand_size < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Hand size too big"));
            }
            player_hand_sizes.push(hand_size);
        }
        section_base += num_players as usize * 2;

        // Player names
        let mut player_names = Vec::new();
        for _ in 0..num_players {
            if bytes.len() < section_base + 1 {
                return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Start frame does not contain player name length"));
            }
            let player_name_len = bytes[section_base] as usize;
            if bytes.len() < section_base + 1 + player_name_len {
                return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Start frame does not contain player name"));
            }
            if let Ok(player_name) = String::from_utf8(bytes[section_base + 1..section_base + 1 + player_name_len].to_vec()) {
                player_names.push(player_name);
            } else {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Failed to decode player name"));
            }
        }

        // Combine player info into one vec
        let mut players_info = Vec::new();
        for player_id in player_order {
            if player_id as usize >= player_hand_sizes.len() {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Player ID out of bounds"));
            }
            players_info.push(Player {
                id: player_id as i8,
                name: player_names.get(player_id as usize).unwrap().clone(),
                hand_size: *player_hand_sizes.get(player_id as usize).unwrap(),
            });
        }
        return Ok(StartFrame { 
            hand: cards,
            players: players_info,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_START;
    }
}

const FRAME_TYPE_TURN: i8 = 5;
pub struct TurnFrame {
    pub player_id: i8,
}
impl Frame<TurnFrame> for TurnFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.player_id as u8);
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<TurnFrame, std::io::Error> {
        if bytes.len() < 1 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete turn frame"));
        }
        return Ok(TurnFrame {
            player_id: bytes[0] as i8,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_TURN;
    }
}

const FRAME_TYPE_ACTION: i8 = 6;
pub struct ActionFrame {
    pub player_id: i8,
    pub responder_id: i8,
    pub suggestion: Vec<i16>,
    pub is_solving: bool,
}
impl Frame<ActionFrame> for ActionFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.player_id as u8);
        bytes.push(self.responder_id as u8);
        bytes.push(if self.is_solving { 1 } else { 0 });
        bytes.push(0);
        for card in self.suggestion.iter() {
            let card_big_endian = (*card).to_be_bytes();
            bytes.push(card_big_endian[0]);
            bytes.push(card_big_endian[1]);
        }
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<ActionFrame, std::io::Error> {
        if bytes.len() < 4 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete action frame"));
        }
        let num_cards = bytes.len() / 2 - 1;
        let mut cards = Vec::new();
        for card_idx in 0..num_cards {
            let card_big_endian: [u8; 2] = [bytes[2 + card_idx * 2], bytes[2 + card_idx * 2 + 1]];
            let card = i16::from_be_bytes(card_big_endian);
            if card < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Card ID out of bounds"));
            }
            cards.push(card);
        }

        return Ok(ActionFrame {
            player_id: bytes[0] as i8,
            responder_id: bytes[1] as i8,
            suggestion: cards,
            is_solving: bytes[2] != 0,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_ACTION;
    }
}

const FRAME_TYPE_REPLY: i8 = 7;
pub struct ReplyFrame {
    pub player_id: i8,
    pub card: i16,
}
impl Frame<ReplyFrame> for ReplyFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.player_id as u8);
        bytes.push(0);
        let card_big_endian = (self.card).to_be_bytes();
        bytes.push(card_big_endian[0]);
        bytes.push(card_big_endian[1]);
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<ReplyFrame, std::io::Error> {
        if bytes.len() < 4 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete reply frame"));
        }
        let card_big_endian: [u8; 2] = [bytes[2], bytes[3]];
        let card = i16::from_be_bytes(card_big_endian);
        if card < 0 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Card ID out of bounds"));
        }
        return Ok(ReplyFrame {
            player_id: bytes[0] as i8,
            card: card,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_REPLY;
    }
}

const FRAME_TYPE_GAME_END: i8 = 8;
pub struct GameEndFrame {
    pub winner: i8,
    pub won_by_default: bool,
}
impl Frame<GameEndFrame> for GameEndFrame {
    fn serialize(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.winner as u8);
        bytes.push(if self.won_by_default { 1 } else { 0 });
        return bytes;
    }

    fn deserialize(bytes: &[u8]) -> Result<GameEndFrame, std::io::Error> {
        if bytes.len() < 2 {
            return Err(std::io::Error::new(std::io::ErrorKind::Interrupted, "Incomplete game end frame"));
        }
        return Ok(GameEndFrame {
            winner: bytes[0] as i8,
            won_by_default: bytes[1] != 0,
        });
    }

    fn get_type() -> i8 {
        return FRAME_TYPE_GAME_END;
    }
}

pub fn send_frame<F: Frame<F>>(writer: &mut std::net::TcpStream, frame: &F) -> Result<(), std::io::Error> {
    let frame_bytes = frame.serialize();
    let frame_header = FrameHeader {
        frame_type: F::get_type(),
        data_length: frame_bytes.len() as i32,
    };
    let frame_header_bytes = frame_header.serialize();
    writer.write_all(frame_header_bytes.as_slice())?;
    writer.write_all(frame_bytes.as_slice())?;

    return Ok(());
}

pub fn expect_frame<F: Frame<F>>(reader: &mut std::net::TcpStream) -> Result<F, std::io::Error> {
    let mut frame_header;
    loop {
        let mut frame_header_bytes: [u8; 8] = [0; 8];
        reader.read_exact(&mut frame_header_bytes[..])?;
        frame_header = FrameHeader::deserialize(&frame_header_bytes)?;

        if frame_header.frame_type == F::get_type() {
            break;
        } else {
            // Some other frame, throw it away
            std::io::copy(&mut reader.take(frame_header.data_length as u64), &mut std::io::sink())?;
        }
    }
    let mut frame_bytes = vec![0; frame_header.data_length as usize];
    reader.read_exact(frame_bytes.as_mut_slice())?;
    let frame = F::deserialize(frame_bytes.as_mut_slice())?;
    return Ok(frame);
}