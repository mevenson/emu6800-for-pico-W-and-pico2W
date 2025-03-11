/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <time.h>
 
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
 
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP
#include "tcp.h"
 
struct tcp_pcb *tcp_client_pcb;
volatile bool done = false;

uint8_t  *packetDataPtr;
uint8_t  *responseDataPtr;
uint16_t dataLength;
uint16_t responseLength;

uint16_t responseDataPointer;

// Callback for receiving data
//
//      this keeps getting called as long as there more data received and tcp_recved is called
//      to acknowledge receipt of this packet
err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) 
{
    // we are done when the server closed the conneciton
    if (!p) 
    { 
        // Connection closed by server
        tcp_close(tpcb);
        done = true;

        return ERR_OK;
    }
    
    //printf("Received %d bytes\n", p->tot_len);

    for (int i = 0; i < p->len; i++)
    {
        responseDataPtr[responseDataPointer++] = ((uint8_t *)p->payload)[i];
    }
    responseLength += p->len;

    //printf("Received: %.*s\n", p->len, (char *)p->payload);

    tcp_recved(tpcb, p->len);   // Acknowledge received data to get tcp_recv_callback called if more data is available
    pbuf_free(p);               // Free received buffer

    return ERR_OK;
}

// Callback for connection success - this is where the drequest packet is sent to the server. Once
// the data is sent the response is gathered in the receive callback
err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) 
{
    err_t returnValue = !ERR_OK;        // default to failed

    //printf("TCP Connected Callback! err=%d\n", err);
    if (err == !ERR_OK) 
    {
        printf("Connection failed with error: %d\n", err);
        returnValue == err;
    }
    else
    {
        //printf("Connected to server!\n");
        tcp_recv(tpcb, tcp_recv_callback); // Set receive callback

        // Send data
        //const char *msg = "Hello from Pico W!";
        err_t sent_err = tcp_write(tpcb, packetDataPtr, dataLength, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb); // Flush data

        if (sent_err == ERR_OK) 
        {
            //printf("Message sent to server!\n");
        }
        else 
        {
            printf("Failed to send message\n");
        }
        returnValue = sent_err;
    }
    return returnValue;
}

// Function to start the TCP client - all this does is create the connection. Once the connection is
// made, the connection callback kicks in and actually sends the packet and receives the response.
err_t start_tcp_client() 
{
    err_t returnValue = !ERR_OK;        // default to failed

    tcp_client_pcb = tcp_new();
    if (tcp_client_pcb) 
    {
        ip_addr_t server_addr;
        ip4addr_aton(TCP_SERVER_IP, &server_addr);

        err_t err = tcp_connect(tcp_client_pcb, &server_addr, TCP_PORT, tcp_connected_callback);
        returnValue = err;
    }
    else
    {
        printf("Failed to create PCB\n");
    }

    return(returnValue);
}

// Make an TCP request - adapted from ntp_request in ntpexample
//
//  it will put the response in the responsePacket and return the length of the responsePacket
uint16_t tcp_request(uint8_t *packetData, uint8_t *responseData, int packetLength) 
{
    responseDataPointer = 0;
    responseLength = 0;
    done = false;

    packetDataPtr   = packetData;
    responseDataPtr = responseData;
    dataLength      = packetLength;

    // first we start off the chain of events by connecting to the server. This will cause the connection callback to be
    // esecutes which will send the data to the server. Once the data has been sent the response will be gathered by the
    // receive callback. These events are all done asynchronously, so we need to knwo when they are complete. Since each 
    // event sets up what will happen next, we can set a semaphore on the last event to knwo when we are done.

    err_t err =start_tcp_client();
    if (err == ERR_OK) 
    {
        while (!done)      // wait for response
        {
            cyw43_arch_poll(); 
            sleep_ms(10);
        }
    }
    else
        printf("error connecting to FLEXNet Server\n");

    return (responseLength);
}
