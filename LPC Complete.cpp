#include "mbed.h"
#include "MFRC522.h"
#include "nRF24L01P.h"
#include <vector>
#include <iostream>
#include <bitset>
#include <iomanip>
#include <string>

// Pin configuration for MFRC522 (adjust based on your wiring)
#define SPI_MOSI p11
#define SPI_MISO p12
#define SPI_SCK  p13
#define SPI_CS   p28
#define SPI_RST  p10

// nRF24L01+ pin configuration
nRF24L01P my_nrf24l01p(p5, p6, p7, p30, p29, p16); // mosi, miso, sck, csn, ce, irq

// MFRC522 RFID module instance
MFRC522 rfid(SPI_MOSI, SPI_MISO, SPI_SCK, SPI_CS, SPI_RST);

DigitalIn switch1(p17);
DigitalIn switch2(p18);
DigitalIn switch3(p19);
DigitalIn switch4(p20);

DigitalOut myled1(LED1);

// Constants for nRF24L01+
#define TRANSFER_SIZE   32
#define ADDRESS_WIDTH   5

char txData[TRANSFER_SIZE], rxData[TRANSFER_SIZE];
int txDataCnt = 0, rxDataCnt = 0;

// Define Block128 as a vector of 16 bytes (128 bits)
using Block128 = std::vector<uint8_t>;

// Combine RFID UID and DIP switch values into a 128-bit key
Block128 generateKey(const MFRC522::Uid& uid) {
    Block128 key(16, 0); // Initialize key with 16 bytes

    // Use RFID UID (up to 12 bytes) for key
    for (size_t i = 0; i < std::min((size_t)uid.size, (size_t)12); ++i) {  // Cast uid.size to size_t
        key[i] = uid.uidByte[i];
    }

    // Get the 4-bit DIP switch values (combined into 1 byte)
    uint8_t dipswitchValue = (switch1.read() << 3) | (switch2.read() << 2) |
                            (switch3.read() << 1) | switch4.read();

    // Debug print to show the DIP switch values
    printf("DIP Switches: %u%u%u%u (Value: %02X)\n", 
            switch1.read(), switch2.read(), switch3.read(), switch4.read(), dipswitchValue);

    // Store the DIP switch value in the last byte of the key
    key[15] = dipswitchValue;

    return key;
}

// Simple XOR-based block cipher for a single block
void blockCipherEncrypt(Block128& block, const Block128& key) {
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] ^= key[i];
    }
}

void blockCipherDecrypt(Block128& block, const Block128& key) {
    blockCipherEncrypt(block, key); // XOR is symmetric
}

void printBlock(const Block128& block) {
    for (uint8_t byte : block) {
        printf("%02X ", byte);
    }
    printf("\n");
}

// Function to send data via nRF24L01+
void sendData(const char* data, int size) {
    my_nrf24l01p.write(NRF24L01P_PIPE_P0, rxData, sizeof(rxData));
    printf("Sent data: ");
    for (int i = 0; i < size; ++i) {
        printf("%02X ", (unsigned char)data[i]);
    }
    printf("\n");
}

void nrf24_task() {
    if (my_nrf24l01p.readable()) {
        // Read incoming data
        rxDataCnt = my_nrf24l01p.read(NRF24L01P_PIPE_P0, rxData, sizeof(rxData));
        printf("Received data of size %d: ", rxDataCnt);
        for (int i = 0; i < rxDataCnt; i++) {
            printf("%c", rxData[i]);
        }
        printf("\n");
    }
}

int main() {
    printf("Initializing system...\n");

    // Initialize RFID module
    rfid.PCD_Init();

    // Initialize nRF24L01+
    my_nrf24l01p.powerUp();
    my_nrf24l01p.setRfOutputPower(-6);
    my_nrf24l01p.setTxAddress(0x1F22676D9, ADDRESS_WIDTH); // Set the Tx address
    my_nrf24l01p.setRxAddress(0x1F22676D9, ADDRESS_WIDTH); // Set the Rx address
    my_nrf24l01p.setAirDataRate(2000);
    my_nrf24l01p.setTransferSize(TRANSFER_SIZE);
    my_nrf24l01p.setReceiveMode();
    my_nrf24l01p.enable();

    // Display nRF24L01+ configuration
    printf("nRF24L01+ Frequency    : %d MHz\n", my_nrf24l01p.getRfFrequency());
    printf("nRF24L01+ Output power : %d dBm\n", my_nrf24l01p.getRfOutputPower());
    printf("nRF24L01+ Data Rate    : %d kbps\n", my_nrf24l01p.getAirDataRate());
    printf("nRF24L01+ TX Address   : 0x%010llX\n", my_nrf24l01p.getTxAddress());
    printf("nRF24L01+ RX Address   : 0x%010llX\n", my_nrf24l01p.getRxAddress());

    // Wait for RFID input
    while (true) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            // Generate encryption key from RFID UID and DIP switch values
            Block128 key = generateKey(rfid.uid);

            // Print the generated key
            printf("Generated Key (with UID and DIP): ");
            printBlock(key);

            // Perform encryption with the generated key
            // Sample data to encrypt
            Block128 block = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                              0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50}; // "ABCDEFGH"

            printf("Original Block:\n");
            printBlock(block);

            // Encrypt block with the generated key
            blockCipherEncrypt(block, key);
            printf("Encrypted Block:\n");
            printBlock(block);

            // Send the encrypted block via nRF24L01+
            sendData(reinterpret_cast<const char*>(block.data()), block.size());

            // Decrypt block to verify
            blockCipherDecrypt(block, key);
            printf("Decrypted Block:\n");
            printBlock(block);
        }
        nrf24_task();
        ThisThread::sleep_for(100ms); // Check for RFID tags periodically
    }
}
