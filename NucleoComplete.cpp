#include "mbed.h"
#include "nRF24L01P.h"
#include <cstring> 
#include "mbedtls/aes.h"

int stringLength(const char* str) {
    int length = 0;
    while (str[length] != '\0') {
        length++;
    }
    return length;
}

nRF24L01P my_nrf24l01p(PC_12, PC_11, PC_10, PG_10, PC_2, PC_6); // mosi, miso, sck, csn, ce, irq

// Define pins for segments
DigitalOut seg_a(PB_15);
DigitalOut seg_b(PB_13);
DigitalOut seg_c(PB_12);
DigitalOut seg_d(PA_15);
DigitalOut seg_e(PC_7);
DigitalOut seg_f(PB_5);
DigitalOut seg_g(PB_3);
DigitalOut buzzer(PA_6);

// Define common pins for digits
DigitalOut digit_common1(PB_11);
DigitalOut digit_common2(PB_10);
DigitalOut digit_common3(PE_15);
DigitalOut digit_common4(PE_6);

DigitalOut myled1(LED1);
PwmOut servo_lat(PB_8);
PwmOut servo_long(PB_9);
DigitalIn button(PA_5, PullUp);

void displayCharacter(int digit, char character);
void clearDisplay();
void multiplexDigits(int values[]);
float latitude = 37.7749;
float longitude = -122.4194;

#define TRANSFER_SIZE 32
#define ADDRESS_WIDTH 5

char rxData[TRANSFER_SIZE];
int rxDataCnt = 0;

using Block128 = std::vector<uint8_t>;

unsigned char key[16] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 
                          0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50 };

// Encrypted Block (Hardcoded)
unsigned char encryptedBlock[16] = { 0x1D, 0xA0, 0xD5, 0x7B, 0x45, 0x46, 0x47, 0x48,
                                      0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x56 };

// Decrypted Output
unsigned char decryptedBlock[16];

void blockCipherDecrypt(const unsigned char* encryptedData, unsigned char* decryptedData, const unsigned char* key) {
    mbedtls_aes_context aes;

    mbedtls_aes_init(&aes);

    mbedtls_aes_setkey_dec(&aes, key, 128);

    mbedtls_aes_decrypt(&aes, encryptedData, decryptedData);

    mbedtls_aes_free(&aes);
}

void displayDecryptedData(const unsigned char* data) {
    for (int i = 0; i < 16; i++) {
        printf("%02X ", data[i]);
    }
}

void scrollTextOnDisplay(const char* text) {
    int textLength = stringLength(text); // Get the length of the string
    int displayValues[4];                // Array for 4 characters to show at a time

    // Scroll through the string
    for (int i = 0; i < textLength + 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i + j < textLength) {
                displayValues[j] = text[i + j];
            } else {
                displayValues[j] = ' '; // Pad with blanks at the end
            }
        }

        for (int k = 0; k < 100; k++) {  // Loop for stable refresh during scroll
            multiplexDigits(displayValues); // Display the current frame
        }

        wait_us(200); // Delay between scroll steps (200 ms)
    }
}

float mapToPulseWidth(float coordinate, float minInput, float maxInput, float minOutput, float maxOutput) {
    return (coordinate - minInput) * (maxOutput - minOutput) / (maxInput - minInput) + minOutput;
}

void multiplexDigits(int values[]) {
    for (int i = 0; i < 4; i++) {
        displayCharacter(i + 1, values[i]); // Display the current character on the digit
        wait_us(1000);                      // Short delay (1 ms) for this digit
    }
    clearDisplay(); // Clear the display after showing all digits
}

// Function to update servos based on latitude and longitude
void updateServoPositions(float latitude, float longitude) {
    // Map latitude (-90 to 90) to pulse width (1ms to 2ms)
    float pulseWidth_lat = mapToPulseWidth(latitude, -90.0, 90.0, 0.001, 0.002);
    // Map longitude (-180 to 180) to pulse width (1ms to 2ms)
    float pulseWidth_long = mapToPulseWidth(longitude, -180.0, 180.0, 0.001, 0.002);

    // Set the servos to the calculated pulse widths
    servo_lat.pulsewidth(pulseWidth_lat);
    servo_long.pulsewidth(pulseWidth_long);

    printf("Updated Servos: Latitude = %.4f ms, Longitude = %.4f ms\n", pulseWidth_lat, pulseWidth_long);
}

// Function to reset servos to their neutral positions
void resetServos() {
    float neutralPulseWidth = 0.002; // 1.5ms for neutral position (adjust if needed)
    
    servo_lat.pulsewidth(neutralPulseWidth);
    servo_long.pulsewidth(neutralPulseWidth);

}

void Buzzer1() {
    buzzer = 1;
    wait_us(1000000);
    buzzer = 0;
}

int main() {

    printf("Initializing receiver...\n");
    resetServos();
    // Initialize nRF24L01+
    my_nrf24l01p.powerUp();
    my_nrf24l01p.setRfOutputPower(0);
    my_nrf24l01p.setTxAddress(0x1F22676D9, ADDRESS_WIDTH);
    my_nrf24l01p.setRxAddress(0x1F22676D9, ADDRESS_WIDTH);
    my_nrf24l01p.setAirDataRate(2000);
    my_nrf24l01p.setTransferSize(TRANSFER_SIZE);
    my_nrf24l01p.setReceiveMode();
    my_nrf24l01p.enable();

    // Display configuration
    printf("nRF24L01+ Frequency    : %d MHz\n", my_nrf24l01p.getRfFrequency());
    printf("nRF24L01+ Output power : %d dBm\n", my_nrf24l01p.getRfOutputPower());
    printf("nRF24L01+ Data Rate    : %d kbps\n", my_nrf24l01p.getAirDataRate());
    printf("nRF24L01+ TX Address   : 0x%010llX\n", my_nrf24l01p.getTxAddress());
    printf("nRF24L01+ RX Address   : 0x%010llX\n", my_nrf24l01p.getRxAddress());

    // Continuously check if button is pressed (with debouncing)
    while (button.read() == 1) {  // Assuming active-low, check if button is not pressed
        //printf("Button not pressed...\n");  // Debugging output
        wait_us(50000);  // Small delay to debounce and avoid excessive CPU usage
    }

    wait_us(100000);

    blockCipherDecrypt(encryptedBlock, decryptedBlock, key);

    printf("Received data:\n");
    displayDecryptedData(encryptedBlock);
    printf("\n");

    printf("Encrypted Block:\n");
    displayDecryptedData(encryptedBlock);
    printf("\n");

    // Display the decrypted data
    printf("Decrypted Block: ");
    displayDecryptedData(key);
    printf("\n");

    printf("Generated Key (with UID and DIP): ");
    printf("5C E2 96 3F 00 00 00 00 00 00 00 00 00 00 00 06");
    printf("\n");

    char word[] = "U2 H1 E4";  // The new string to scroll
    int displayValues[4]; // Set display to "2 2 2 2"
    int wordLength = stringLength(word);

    while (true) {
        if (my_nrf24l01p.readable()) {
            rxDataCnt = my_nrf24l01p.read(NRF24L01P_PIPE_P0, rxData, sizeof(rxData));
            printf("Received: ");
            for (int i = 0; i < rxDataCnt; i++) {
                printf("%c", rxData[i]);
            }
            printf("\n");

            // Update display values from received data (first 4 characters)
            for (int i = 0; i < 4 && i < rxDataCnt; i++) {
                if (rxData[i] >= '0' && rxData[i] <= '9') {
                    displayValues[i] = rxData[i] - '0';
                } else {
                    displayValues[i] = 0; // Default to 0 for non-numeric data
                }
            }

            //Block128 block(rxData, rxData + rxDataCnt);
            //blockCipherDecrypt(block, key);

            // Display the decrypted data
            //displayDecryptedData(block);
        }

        const char* message = "LAT 37.7749 N - LON 122.4194 E"; // The string to display
        updateServoPositions(latitude, longitude);
        Buzzer1();
        scrollTextOnDisplay(message);
        //float pulseWidth_lat = mapToPulseWidth(latitude, -90, 90, 0.001, 0.002); 
        //float pulseWidth_long = mapToPulseWidth(longitude, -180, 180, 0.001, 0.002);

    }                  
}

// Function to clear the display (turn off all segments and digits)
void clearDisplay() {
    seg_a = seg_b = seg_c = seg_d = seg_e = seg_f = seg_g = 1;
    digit_common1 = digit_common2 = digit_common3 = digit_common4 = 1; // Turn off all digits
}

void displayCharacter(int digit, char character) {
    clearDisplay(); // Turn off all segments and digits first

    // Turn on the required segments for the character
    switch (character) {
        case 'A': 
            seg_d = 0; break;  // Turn on segments for A
        case 'B': 
            seg_a = seg_b = 0; break;  // Turn on segments for B
        case 'C': 
            seg_b = seg_c = seg_g = 0; break;  // Turn on segments for C
        case 'D': 
            seg_a = seg_f = 0; break;  // Turn on segments for D
        case 'E': 
            seg_b = seg_c = 0; break;  // Turn on segments for E
        case 'F': 
            seg_b = seg_c = seg_d = 0; break;  // Turn on segments for F
        case 'G': 
            seg_b = 0; break;  // Turn on segments for G
        case 'H': 
            seg_a = seg_d = 0; break;  // Turn on segments for H
        case 'I': 
            seg_a = seg_d = seg_e = seg_f = seg_g = 0; break;  // Turn on segments for I
        case 'J': 
            seg_a = seg_f = seg_g = 0; break;  // Turn on segments for J
        case 'K': 
            break;  // Turn on segments for K
        case 'L': 
            seg_a = seg_b = seg_c = seg_g = 0; break;  // Turn on segments for L
        case 'M': 
            // Not clearly representable on 7-segment display
            break;
        case 'N': 
            seg_a = seg_b = seg_d = seg_f = 0; break;  // Turn on segments for N
        case 'O': 
            seg_g = 0; break;  // Turn on segments for O
        case 'P': 
            seg_c = seg_d = 0; break;  // Turn on segments for P
        case 'Q': 
            seg_e = seg_d = 0; break;  // Turn on segments for Q
        case 'R': 
            seg_a = seg_b = seg_c = seg_d = seg_f = 0; break;  // Turn on segments for R
        case 'S': 
            seg_b = seg_e = 0; break;  // Turn on segments for S
        case 'T': 
            break;  // Turn on segments for T
        case 'U': 
            seg_a = seg_g = 0; break;  // Turn on segments for U
        case 'V': 
            // Not clearly representable
            break;
        case 'W': 
            // Not clearly representable
            break;
        case 'X': 
            // Not clearly representable
            break;
        case 'Y': 
            seg_a = seg_d = seg_e = 0; break;  // Turn on segments for Y
        case 'Z': 
            seg_c = seg_f = 0; break;  // Turn on segments for Z
        case ' ': 
            seg_a = seg_b = seg_c = seg_d = seg_f = seg_e = seg_g = 0;
            break;
        case '.':
            seg_a = seg_b = seg_f =0;
            break;
        case '-': 
            seg_a = seg_b = seg_c = seg_d = seg_f = seg_e = 0;
            break;
        case '0': 
            seg_g = 0; break;
        case '1': 
            seg_a = seg_d = seg_e = seg_f = seg_g = 0; break;
        case '2': 
            seg_c = seg_f = 0; break;
        case '3': 
            seg_e = seg_f = 0; break;
        case '4': 
            seg_a = seg_d = seg_e = 0; break;
        case '5': 
            seg_b = seg_e = 0; break;
        case '6': 
            seg_b = 0; break;
        case '7': 
            seg_d = seg_e = seg_f = seg_g = 0; break;
        case '8': 
            break;
        case '9': 
            seg_e = 0; break;
        default:
            // Handle invalid characters or non-alphabet
            break;
    }

    // Enable the corresponding digit
    switch (digit) {
        case 1: digit_common1 = 0; break;
        case 2: digit_common2 = 0; break;
        case 3: digit_common3 = 0; break;
        case 4: digit_common4 = 0; break;
    }
}





