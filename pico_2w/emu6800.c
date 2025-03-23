#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "hardware/irq.h"

#include "hardware/watchdog.h"

#define EMU6800
#include "emu6800.h"
#include "cpu.h"
#include "fd2.h"
#include "sdcard.h"
#include "mc146818.h"
#include "tcp.h"

// need this to define the external ROM files
//-------------------------------------------
extern const uint8_t swtbuga_v1_303[];
extern const uint8_t newbug[];
extern const uint8_t swtbug_justtherom[];

extern const size_t sizeof_swtbuga_v1_303;
extern const size_t sizeof_newbug;
extern const size_t sizeof_swtbug_justtherom;
//-------------------------------------------

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

#define RESET_BUTTON_PIN 8  // use GPIO8 as the reset button
#define POWER_CYCLE_PIN  9  // use GPIO9 to reset the RP2040 to simulate a power off / power on cycle.

#define CONFIG_BIT_0 18
#define CONFIG_BIT_1 19
#define CONFIG_BIT_2 20
#define CONFIG_BIT_3 21

void init_buttons() 
{
    // Initialize the reset and power cycle button pins as input with a pull-up resistor
    gpio_init(RESET_BUTTON_PIN); gpio_set_dir(RESET_BUTTON_PIN, GPIO_IN); gpio_pull_up(RESET_BUTTON_PIN);
    gpio_init(POWER_CYCLE_PIN ); gpio_set_dir(POWER_CYCLE_PIN , GPIO_IN); gpio_pull_up(POWER_CYCLE_PIN );
}

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

uint32_t set_configuration ()
{
    //  Let's use GPIO18, GPIO19, GPIO20 and CPIO21 to select one of 16 possible configurations. Since
    //  the GPIO pins when set as inputs can have internal pull up or pull down resistors, leaving them
    //  not connected to +3.3V or ground will cause the pin to register as 0 (pulled low) if we connect
    //  them to the pull down resistor. So if you want configuration 0, leave them all disconnected. To 
    //  select confguration 1, you would only have to connect GPIO18 to 3.3V.
    //  
    //          to select configuration tie the pins to 3.3V to select the bit
    //
    //      the upper two bits are always 0 because the pico only has 30 GPIO pins
    //
    //
    //                                                      GPIO pin numbers
    //                                                      ----------------
    //
    //                                          ---------------
    //              3 3 2 2     2 2 2 2     2 2 | 2 2     1 1 | 1 1     1 1 1 1     1 1 0 0     0 0 0 0     0 0 0 0
    //              1 0 9 8     7 6 5 4     3 2 | 1 0     9 8 | 7 6     5 4 3 2     1 0 9 8     7 6 5 4     3 2 1 0
    //              -------     -------     ----| ---     ----| ---     -------     -------     -------     -------
    //              0 0                         | ^ ^     ^ ^ | ^ ^     ^ ^ ^ ^     ^ ^ ^ ^     ^ ^ ^ ^     ^ ^ ^ ^
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |     | | | |____ GPIO 0                        
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |     | | |______ GPIO 1
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |     | |________ GPIO 2
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |     |__________ GPIO 3
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | | |________________ GPIO 4   UART TX to FTDI
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | | |__________________ GPIO 5   UART RX to FTDI
    //              0 0                         | | |     | | | | |     | | | |     | | | |     | |____________________ GPIO 6
    //              0 0                         | | |     | | | | |     | | | |     | | | |     |______________________ GPIO 7
    //              0 0                         | | |     | | | | |     | | | |     | | | |
    //              0 0                         | | |     | | | | |     | | | |     | | | |____________________________ GPIO 8   BUTTON 0 (RESET)
    //              0 0                         | | |     | | | | |     | | | |     | | |______________________________ GPIO 9   BUTTON 1 (POWER OFF/ON)
    //              0 0                         | | |     | | | | |     | | | |     | |________________________________ GPIO 10
    //              0 0                         | | |     | | | | |     | | | |     |__________________________________ GPIO 11
    //              0 0                         | | |     | | | | |     | | | |
    //              0 0                         | | |     | | | | |     | | | |________________________________________ GPIO 12
    //              0 0                         | | |     | | | | |     | | |__________________________________________ GPIO 13
    //              0 0                         | | |     | | | | |     | |____________________________________________ GPIO 14  RD_LED_PIN for SDCARD
    //              0 0                         | | |     | | | | |     |______________________________________________ GPIO 15  WR_LED_PIN for SDCARD
    //              0 0                         | | |     | | | | |        
    //              0 0                         | | |     | | | | |____________________________________________________ GPIO 16  RD_LED_PIN for FD2
    //              0 0                         | | |     | | | |______________________________________________________ GPIO 17  WR_LED_PIN for FD2
    //              0 0                         | | |     | | |          
    //              0 0                         | | |     | |_|________________________________________________________ GPIO 18 --
    //              0 0                         | | |     |___|________________________________________________________ GPIO 19   |_ see explanation below
    //              0 0                         | | |_________|________________________________________________________ GPIO 20   |
    //              0 0                         | |___________|________________________________________________________ GPIO 21 --
    //                                          |             |
    //                                          |             |                                                             selected config (ROM to load for now)
    // 0x3800030    0 0 0 0     0 0 1 1     1 0 | 0 0     0 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0000       0 PT68-1 and SWTPC (has SD Card)
    // 0x3840030    0 0 0 0     0 0 1 1     1 0 | 0 0     0 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0001       1 SWTPC without SD Card
    // 0x3880030    0 0 0 0     0 0 1 1     1 0 | 0 0     1 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0010       2 CP68 for SWTPC without SD Card
    // 0x38C0030    0 0 0 0     0 0 1 1     1 0 | 0 0     1 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0011       3 
    // 0x3900030    0 0 0 0     0 0 1 1     1 0 | 0 1     0 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0100       4 
    // 0x3940030    0 0 0 0     0 0 1 1     1 0 | 0 1     0 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0101       5 
    // 0x3980030    0 0 0 0     0 0 1 1     1 0 | 0 1     1 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0110       6 
    // 0x39C0030    0 0 0 0     0 0 1 1     1 0 | 0 1     1 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   0111       7 
    // 0x3A00030    0 0 0 0     0 0 1 1     1 0 | 1 0     0 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1000       8 
    // 0x3A40030    0 0 0 0     0 0 1 1     1 0 | 1 0     0 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1010       9 
    // 0x3A80030    0 0 0 0     0 0 1 1     1 0 | 1 0     1 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1010      10 
    // 0x3AC0030    0 0 0 0     0 0 1 1     1 0 | 1 0     1 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1011      11 
    // 0x3B00030    0 0 0 0     0 0 1 1     1 0 | 1 1     0 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1100      12 
    // 0x3B40030    0 0 0 0     0 0 1 1     1 0 | 1 1     0 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1101      13 
    // 0x3B80030    0 0 0 0     0 0 1 1     1 0 | 1 1     1 0 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1110      14 
    // 0x3BC0030    0 0 0 0     0 0 1 1     1 0 | 1 1     1 1 | 0 0     0 0 0 0     0 0 0 0     0 0 1 1     0 0 0 0   1111      15 
    //                                          ---------------                                                       ^^^^
    //                                                                                                                ||||__ GPIO18
    //                                                                                                                |||___ GPIO19
    //                                                                                                                ||____ GPIO20
    //                                                                                                                |_____ GPIO21
    //                                                                                                                 

    // set the pins as inputs and connect them to the internal pull down resisters so that when
    // nothing is selected, it defaults to selection 0.

    gpio_init(CONFIG_BIT_0); gpio_set_dir(CONFIG_BIT_0, GPIO_IN); gpio_pull_down(CONFIG_BIT_0);
    gpio_init(CONFIG_BIT_1); gpio_set_dir(CONFIG_BIT_1, GPIO_IN); gpio_pull_down(CONFIG_BIT_1);
    gpio_init(CONFIG_BIT_2); gpio_set_dir(CONFIG_BIT_2, GPIO_IN); gpio_pull_down(CONFIG_BIT_2);
    gpio_init(CONFIG_BIT_3); gpio_set_dir(CONFIG_BIT_3, GPIO_IN); gpio_pull_down(CONFIG_BIT_3);

    // now read them to determin configuration
    uint32_t gpio_pins = gpio_get_all();

    selectedConfiguration = (uint8_t)(gpio_pins >> CONFIG_BIT_0) & 0x0000000F;

    // now flip the bits so allopen = 0x00 and all tied to +3.3 = 0x0f
    // only the lower 4 bits are used.

    //selectedConfiguration ^= 0x0F;

    return gpio_pins;
}

int main()
{
    // Set up our UART
    setup_uart1();
    init_buttons();

    uint32_t gpio_pins = set_configuration();

    printf("User selected configuration profile %d\n", selectedConfiguration);
    printf("Setting up environment to run 6800 emulator\n");
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
        initialize_sdcard();
    }
    else
        printf("Failed to initialize the WIFI conneciton for floppy and mc146818\n");

    
    printf("Starting 6800 Emulator...\n");

    // run the emulator - run_emulator();
    //
    //      This code used to be in the cpu.c file, but I moved it here so I could
    //      intercept the REST button tied to GPIO8 during the emulation of the
    //      6800 instruction set. Yhe code to rest needed to be in the main while 
    //      loop and that is here.

    int numberOfBreakPointEntries = sizeof(breakpoints) / sizeof(BREAKPOINT);    // there are 8 bytes per entry - 4 for 

    // we are going to pick the rom to load based on the configuration set by GPIO18 through GPIO21.
    //
    //      using the following logic:
    //
    //      configuraiton   0   = swtbuga_v1_303        this is for emulating the PT68-1 (has SD Card)
    //                      1   = newbug                this is for emulating SWTPC without SD Card
    //                      2   = swtbug_justtherom     this is for emulating CP68 without SD Card

    switch (selectedConfiguration)
    {
        case 0:     printf("loading swtbuga_v1_303\n");     load_rom(swtbuga_v1_303,    sizeof_swtbuga_v1_303);    break;
        case 1:     printf("loading newbug\n");             load_rom(newbug,            sizeof_newbug);            break;
        case 2:     printf("loading swtbug_justtherom\n");  load_rom(swtbug_justtherom, sizeof_swtbug_justtherom); break;

        default:    printf("loading swtbuga_v1_303\n");     load_rom(swtbuga_v1_303,    sizeof_swtbuga_v1_303);    break;
    }

    bool running = false;

    while (1) 
    {
        // only do this the first time we enter or on reset

        if (!running)
        {
            cpu.A   = 0;
            cpu.B   = 0;
            cpu.X   = 0;
            cpu.SP  = 0;
            cpu.PC  = cpu.memory[0xFFFE] * 256 + cpu.memory[0xFFFF];
            cpu.CCR = 0;

            running = true;
        }
    
        inWait = 0;
        
        // this is the code that we needed to add to the main while loop
        // so we could restart the emulation

        // Using the watchdog_reset - resets the CPU and basically starts over
        // with the debugger disconnected - not what we want when debugging
        // this is equicalent to a power cycle on the 6800 CPU

        if (gpio_get(POWER_CYCLE_PIN) == 0) 
        {
            printf("Resetting...\n");
            sleep_ms(500);  // Debounce delay
    
            // Perform a software reset using the watchdog to reset the RP2040
            watchdog_reboot(0, 0, 0);
        }

        // just setting running to false will cause the 6800 emulation to start over
        // as if the reset button had been pressed. The debugger stays attached.
        if (gpio_get(RESET_BUTTON_PIN) == 0) 
        {
            printf("Resetting...\n");
            sleep_ms(500);  // Debounce delay
    
            // just reset the 6800 - not the RP2040
            running = false;
        }
        
        if (running)
        {
            for (int i = 0; i < numberOfBreakPointEntries; i++)
            {
                // before we execute the next instruciton - see if it is in the list of breakpoints
                if (breakpoints[i].address == cpu.PC)
                {
                    // if it is - see if we should show the registers and the instruction line from the listing file
                    if (breakpoints[i].printLine)
                    {
                        // show the registers and the instruction line from the listing file
                        printf("\n0x%04X: X=%04X A=%02X B=%02X CCR=%02x %s", cpu.PC, cpu.X, cpu.A, cpu.B, cpu.CCR, breakpoints[i].description);
                    }
                    break;
                }
            }
            execute_instruction();
            if (sendCycles)
            {
                tcp_request(cyclesPacketData, cyclesResponseBuffer, sizeof(cyclesPacketData));
                sendCycles = false;
            }
        }
    }

}
