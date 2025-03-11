#define TCP_SERVER_IP  "10.0.0.91"
#define WIFI_SSID      "xfinity-1410"
#define WIFI_PASSWORD  "creek6612bridge"

#define TCP_PORT     6800
#define DEBUG_printf printf

#ifndef TCP
    // these will be made public
    extern uint16_t tcp_request(uint8_t *packetData, uint8_t *responseData, int packetLength);
#endif

