import serial
import wave
import time

PORT = "COM6"
BAUD = 921600

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2
SECONDS = 5
START_MARKER = b"Audio started\r\n"

bytes_needed = SAMPLE_RATE * CHANNELS * SAMPLE_WIDTH * SECONDS

ser = serial.Serial(PORT, BAUD, timeout=10)
try:
    print("Waiting for MCU audio start marker...")
    startup = ser.read_until(START_MARKER)
    if not startup.endswith(START_MARKER):
        raise RuntimeError("MCU start marker not received; reset the board and try again")

    print(f"Recording {SECONDS}s...")
    print(f"Need {bytes_needed} bytes")

    ser.timeout = 1
    deadline = time.monotonic() + SECONDS + 2
    pcm = bytearray()

    while len(pcm) < bytes_needed and time.monotonic() < deadline:
        chunk = ser.read(min(4096, bytes_needed - len(pcm)))
        if chunk:
            pcm.extend(chunk)

    print(f"Received {len(pcm)} bytes")
    if len(pcm) != bytes_needed:
        raise RuntimeError(
            f"Incomplete audio: expected {bytes_needed} bytes, received {len(pcm)}"
        )

    with wave.open("record.wav", "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)

    print("Saved: record.wav")
finally:
    ser.close()
