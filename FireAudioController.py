import asyncio
import json
from datetime import datetime, timezone
import pyautogui
import serial
import serial.tools.list_ports
from winrt.windows.media.control import (
    GlobalSystemMediaTransportControlsSessionManager as MediaManager,
)

BAUDRATE = 115200 
ser = None

BLUETOOTH_PORT_KEYWORDS = ["bluetooth", "rfcomm"]

async def find_controller() -> str:
    while True:
        ports = await asyncio.to_thread(serial.tools.list_ports.comports)
        for port in ports:
            description = (port.description or "").lower()
            if any(keyword in description for keyword in BLUETOOTH_PORT_KEYWORDS):
                print(f"Bluetooth port found: {port.device} ({port.description})")
                return port.device

        if ports:
            detected = "; ".join(f"{port.device}: {port.description}" for port in ports)
            print(f"ESP32 Bluetooth port is not found. Available ports: {detected}")
        else:
            print("Ports not found. Please check if the ESP32 is connected and Bluetooth is enabled.")
        
        print("Repeat in 5 seconds...")
        await asyncio.sleep(5)


async def main():
    
    # Main cycle of the program. Task: connect to the ESP32 via Bluetooth and start cycle of reading media and sending it to the board.
    
    global ser
    port = await find_controller()
    
    try:
        ser = await asyncio.to_thread(serial.Serial, port, BAUDRATE, timeout=0.5)
        print(f"Successful connection to Bluetooth port {port}")
    except Exception as e:
        print(f"Failed to connect to {port}: {e}")
        await asyncio.sleep(3)
        return await main()
    
    await asyncio.sleep(1)
    asyncio.create_task(read_serial_loop())
    last_media_info = None
    while True:
        media_info = await get_media_info()
        media_snapshot = tuple(media_info.items())
        if media_snapshot != last_media_info:
            print(f"Media info: {media_info}")
            last_media_info = media_snapshot

        await send_message(media_info)
        await asyncio.sleep(0.2)

async def read_serial_loop():
    global ser
    while True:
        try:
            if ser and ser.is_open:
                in_waiting = await asyncio.to_thread(lambda: ser.in_waiting)
                if in_waiting > 0:
                    raw_response = await asyncio.to_thread(ser.readline)
                    response = raw_response.decode("utf-8", errors="ignore").strip()

                    if response:
                        print(f"Received from ESP32: {response}")
                        await recieve_message(response)
        except Exception as e:
            print(f"Serial read error: {e}")
        await asyncio.sleep(0.05)

def trim_to_bytes(text: str, max_bytes: int) -> str:
    encoded = text.encode('utf-8')
    if len(encoded) <= max_bytes:
        return text
    return encoded[:max_bytes].decode('utf-8', errors='ignore')

async def send_message(message: dict):
    MAX_FIELD_BYTES = 60 

    trimmed_message = {
        "title": trim_to_bytes(message.get("title") or "Nothing", MAX_FIELD_BYTES),
        "source": trim_to_bytes(message.get("source") or "playing", MAX_FIELD_BYTES),
        "status": message.get("status", "STOPPED"),
        "time_position": int(message.get("time_position", 0)),
        "time_duration": int(message.get("time_duration", 0)),
    }

    json_str = json.dumps(trimmed_message, ensure_ascii=False, separators=(',', ':')) + "\n"
    
    await asyncio.to_thread(ser.reset_input_buffer)
    await asyncio.to_thread(ser.write, json_str.encode("utf-8"))
    
    for _ in range(10):
        in_waiting = await asyncio.to_thread(lambda: ser.in_waiting)
        if in_waiting > 0:
            raw_response = await asyncio.to_thread(ser.readline)
            response = raw_response.decode("utf-8", errors="ignore").strip()

            if not response:
                continue

            if response == "Handshake received" or response == "OK":
                return
            if response.startswith("Json error:"):
                print(f"ESP32 rejected the JSON: {response}")
                print(f"Tryed to send message, length: {len(json_str.encode('utf-8'))} | Message: {json_str.strip()}")
                return

            print(f"ESP32 serial: {response}")
            await recieve_message(response)
        await asyncio.sleep(0.1)


async def recieve_message(response: str = ""):
    if not response:
        in_waiting = await asyncio.to_thread(lambda: ser.in_waiting)
        if in_waiting == 0:
            return
        raw_response = await asyncio.to_thread(ser.readline)
        response = raw_response.decode("utf-8", errors="ignore").strip()

    match response:
        case "NEXT":
            pyautogui.press("nexttrack")
        case "PREV":
            pyautogui.press("prevtrack")
        case "PAUSEPLAY":
            pyautogui.press("playpause")
        case "MUTE":
            pyautogui.press("volumemute")


async def get_media_info() -> dict:
    try:
        sessions = await MediaManager.request_async()
        current_session = sessions.get_current_session()

        if current_session:
            media_properties = await current_session.try_get_media_properties_async()
            playback_info = current_session.get_playback_info()
            time_info = current_session.get_timeline_properties()

            status = "STOPPED"
            if playback_info and playback_info.playback_status:
                status = playback_info.playback_status.name

            time_pos = 0
            time_dur = 0

            if time_info:
                time_dur = time_info.end_time.total_seconds() if time_info.end_time else 0
                base_pos = time_info.position.total_seconds() if time_info.position else 0
                if status == "PLAYING" and time_info.last_updated_time:
                    last_update = time_info.last_updated_time.astimezone(timezone.utc)
                    now = datetime.now(timezone.utc)
                    elapsed = (now - last_update).total_seconds()
                    time_pos = base_pos + elapsed
                    if time_pos > time_dur:
                        time_pos = time_dur
                else:
                    time_pos = base_pos

            return {
                "title": media_properties.title if media_properties else "",
                "source": media_properties.artist if media_properties else "",
                "status": status,
                "time_position": int(time_pos),
                "time_duration": int(time_dur),
            }
    except Exception as e:
        print(f"Error reading media info: {e}")

    return {
        "title": "",
        "source": "",
        "status": "STOPPED",
        "time_position": 0,
        "time_duration": 0,
    }


if __name__ == "__main__":
    asyncio.run(main())