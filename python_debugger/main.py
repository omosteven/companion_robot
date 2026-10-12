import serial
import struct
import wave

PORT = "/dev/cu.usbmodem14201"
BAUD = 2000000

MAGIC_START = 0x41554430  # AUD0
MAGIC_END   = 0x41554431  # AUD1

print(f"Opening {PORT}...")

with serial.Serial(PORT, BAUD, timeout=5) as ser:

    print("Waiting for audio...")

    # ---------------------------------------------------------
    # Find START marker
    # ---------------------------------------------------------
    while True:
        data = ser.read(4)

        if len(data) != 4:
            raise RuntimeError(
                "Timed out waiting for audio start marker."
            )

        value = struct.unpack("<I", data)[0]

        if value == MAGIC_START:
            break

    print("Audio transmission started.")

    # ---------------------------------------------------------
    # Read sample count
    # ---------------------------------------------------------
    data = ser.read(4)

    if len(data) != 4:
        raise RuntimeError(
            "Failed to read sample count."
        )

    sample_count = struct.unpack("<I", data)[0]

    print(f"Samples: {sample_count}")

    # ---------------------------------------------------------
    # Receive 16-bit PCM
    # ---------------------------------------------------------
    expected_bytes = sample_count * 4

    print(
        f"Receiving {expected_bytes} bytes "
        f"({expected_bytes / 1024:.1f} KiB)..."
    )

    audio_bytes = bytearray()

    while len(audio_bytes) < expected_bytes:

        chunk = ser.read(
            min(
                4096,
                expected_bytes - len(audio_bytes)
            )
        )

        if not chunk:
            raise RuntimeError(
                "Timed out while receiving audio."
            )

        audio_bytes.extend(chunk)

    print("Audio received.")

    # ---------------------------------------------------------
    # Read END marker
    # ---------------------------------------------------------
    # data = ser.read(4)

    # if len(data) != 4:
    #     raise RuntimeError(
    #         "Missing audio end marker."
    #     )

    # value = struct.unpack("<I", data)[0]

    # if value != MAGIC_END:
    #     raise RuntimeError(
    #         f"Invalid end marker: 0x{value:08X}"
    #     )

# -------------------------------------------------------------
# Create WAV
# -------------------------------------------------------------

with wave.open("mic_test.wav", "wb") as wav:

    wav.setnchannels(1)
    wav.setsampwidth(4)
    wav.setframerate(16000)

    wav.writeframes(audio_bytes)

print()
print("===================================")
print("Saved: mic_test2.wav")
print("===================================")