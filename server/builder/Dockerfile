# AirWatch firmware builder: checks GitHub for new firmware commits, builds
# them with your secrets.h (mounted read-only, never stored in the image) and
# leaves the newest build in /data/firmware for the receiver to hand out.
# The image holds only the public toolchain: ESP32 core and libraries at the
# versions the firmware is tested with.
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl git python3 \
 && rm -rf /var/lib/apt/lists/*

ARG ARDUINO_CLI_VERSION=1.3.1
ARG TARGETARCH
RUN case "${TARGETARCH:-amd64}" in \
      amd64) a=Linux_64bit ;; arm64) a=Linux_ARM64 ;; *) echo "unsupported arch ${TARGETARCH}"; exit 1 ;; \
    esac \
 && curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_${a}.tar.gz" \
    | tar -xz -C /usr/local/bin arduino-cli

ENV ESP32_INDEX=https://espressif.github.io/arduino-esp32/package_esp32_index.json
RUN arduino-cli core update-index --additional-urls "$ESP32_INDEX" \
 && arduino-cli core install esp32:esp32@3.3.12 --additional-urls "$ESP32_INDEX" \
 && arduino-cli lib install "Adafruit NeoPixel@1.15.5" "Adafruit GFX Library@1.13.0" \
                            "Adafruit SSD1306@2.5.17" "NimBLE-Arduino@2.5.2" \
 && rm -rf /root/.arduino15/staging

WORKDIR /app
COPY builder.py ./
CMD ["python3", "-u", "builder.py"]
