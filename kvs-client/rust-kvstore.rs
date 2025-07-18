

use std::io::{self, Read, Write};
use std::net::TcpStream;

fn main() -> io::Result<()> {
    let server_address = "192.168.127.141:8000";

    let mut stream = TcpStream::connect(server_address)?;

    stream.write_all(b"GET Student")?;

    let mut buffer = [0; 1024];
    let bytes_read = stream.read(&mut buffer)?;
    println!("Received: {}", String::from_utf8_lossy(&buffer[..bytes_read]));

    Ok(())
}
