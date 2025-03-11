#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <time.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "tcp.h"

void run_emulator();
void tcp();
void initialize_floppy_interface();
bool init_MC146818();

bool wifiSupported = false;
bool timeSupported = false;

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4   // this is TX from the pico (RX on the FTDI)
#define UART_RX_PIN 5   // this is RX from the pico (TX on the FTDI)

void setup_uart1() {
    // Initialize UART1 with 115200 baud rate
    uart_init(UART_ID, 115200);

    // Set the GPIO functions for UART1 TX (GP4) and RX (GP5)
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);  // TX
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);  // RX

    // Redirect printf to UART1
    stdio_uart_init_full(UART_ID, 115200, UART_TX_PIN, UART_RX_PIN);
}

// UART interrupt handler
void on_uart_rx() 
{
    while (uart_is_readable(UART_ID)) 
    {
        uint8_t data = uart_get_hw(UART_ID)->dr & 0xFF; // Read received byte
        printf("Received: %c\n", data);  // Print received character
    }
}

void setup_wifi()
{
    wifiSupported = false;      // default to failed

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) 
    {
        printf("Wi-Fi init failed\n");
    }
    else
    {
        // printf("Wi-Fi init success, CYW43_WL_GPIO_LED_PIN = %d\n", CYW43_WL_GPIO_LED_PIN);

        // Example to turn on the Pico W LED
        //  GPIO 23 is used for the Wi-Fi LED, but it is not directly accessible.
        //  You must use cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, value) to control it.
        //  The function is available in the pico/cyw43_arch.h library.
        //  You must initialize the Wi-Fi chip using cyw43_arch_init() before controlling the LED.

        // GPIO23 - Wi-Fi Status LED
        //  The onboard LED on the Pico W is connected to GPIO23 through the Wi-Fi chip.
        //  You cannot control it directly with gpio_put(23, 1).
        //  Instead, use:
        //      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // Turn LED ON
        //      cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); // Turn LED OFF

        //  GPIO23 → Wi-Fi LED (use cyw43_arch_gpio_put())
        //  GPIO24 → Reserved for Wi-Fi chip (used internally - not available to user)
        //  GPIO25 → Available for user use on Pico W (not connected to an onboard LED)
        //      this is physical pin 37 (labeld EN) on the Pico W)

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

        // Enable wifi station
        cyw43_arch_enable_sta_mode();

        printf("Connecting to Wi-Fi...\n");
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) 
        {
            printf("failed to connect.\n");
        } else 
        {
            printf("Connected.\n");

            // Read the ip address in a human readable way
            uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
            printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);

            wifiSupported = true;
        }
    }

}

int main()
{
    // Set up our UART
    setup_uart1();

    printf("Setting up emvironment to run 6800 emulator\n");
    printf("Connecting to WiFi for floppy and MC146818 support\n");
    setup_wifi();

    /* -- disable this for now
    // Enable the RX interrupt
    uart_set_irq_enables(UART_ID, true, false);  // Enable RX interrupt, disable TX interrupt

    // Attach the interrupt handler
    irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);
    irq_set_enabled(UART1_IRQ, true);
    */

    // TODO: we are connected to the WIFI router - set the status of the connection
    //      do not create the connection to the port at this time. A new connection will
    //      be created for each data exchange with the server. There is no need need for
    //      any two data exchanges to know anything about each other.

    if (wifiSupported)
    {
        printf("connecting to internet time server to initialize MC146818\n");
        timeSupported = init_MC146818();
        printf("connecting to FLEXNet sector server for floppy and SD Card\n");
        initialize_floppy_interface();
    }
    else
        printf("Failed to initialize the WIFI conneciton for floppy and mc146818\n");

    
    printf("Starting 6800 Emulator...\n");
    run_emulator();
}
