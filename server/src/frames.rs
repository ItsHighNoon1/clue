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

struct Player {
    id: i8,
    name: String,
    hand_size: i16,
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
        println!("{}", self.message.len());
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
            Err(_) => Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "Failed to decode connect name")),
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
/*
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
    */

const FRAME_TYPE_START: i8 = 4;
pub struct StartFrame {
    pub hand: Vec<i16>,
    pub players: Vec<Player>,
}

const FRAME_TYPE_TURN: i8 = 5;
pub struct TurnFrame {
    pub player_id: i8,
}

const FRAME_TYPE_SUGGESTION: i8 = 6;
pub struct SuggestionFrame {
    pub player_id: i8,
    pub suggestion: Vec<i16>,
}

const FRAME_TYPE_SUGGESTION_RESPONSE: i8 = 7;
pub struct SuggestionResponseFrame {
    pub player_id: i8,
    pub card: i16,
}

const FRAME_TYPE_SOLVE: i8 = 8;
pub struct SolveFrame {
    pub solution: Vec<i16>,
}

const FRAME_TYPE_SOLVE_RESPONSE: i8 = 9;
pub struct SolveResponseFrame {
    pub player_id: i8,
    pub correct: bool,
    pub solution: Vec<i16>,
}

pub fn send_frame<F: Frame<F>>(writer: &mut std::io::BufWriter<std::net::TcpStream>, frame: &F) -> Result<(), std::io::Error> {
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

pub fn expect_frame<F: Frame<F>>(reader: &mut std::io::BufReader<std::net::TcpStream>) -> Result<F, std::io::Error> {
    let mut frame_header_bytes: [u8; 8] = [0; 8];
    let mut frame_header: FrameHeader;
    loop {
        reader.read_exact(&mut frame_header_bytes[..])?;
        frame_header = FrameHeader::deserialize(&frame_header_bytes)?;
        if frame_header.frame_type == F::get_type() {
            break;
        } else {
            std::io::copy(&mut reader.take(frame_header.data_length as u64), &mut std::io::sink())?;
        }
    }
    let mut frame_bytes = vec![0; frame_header.data_length as usize];
    reader.read_exact(frame_bytes.as_mut_slice())?;
    let frame = F::deserialize(frame_bytes.as_mut_slice())?;
    return Ok(frame);
}