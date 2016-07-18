/*
 * KubOS RT
 * Copyright (C) 2016 Kubos Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"


#include "kubos-hal/gpio.h"
#include "kubos-hal/uart.h"

#include <csp/csp.h>
#include <csp/arch/csp_thread.h>
#include <csp/drivers/usart.h>
#include <csp/interfaces/csp_if_kiss.h>

#define MY_ADDRESS 1
#define MY_PORT    10
#define BLINK_MS 100

static xQueueHandle button_queue;

/* Setup CSP interface */
static csp_iface_t csp_if_kiss;
static csp_kiss_handle_t csp_kiss_driver;

static inline void blink(int pin) {
    k_gpio_write(pin, 1);
    vTaskDelay(BLINK_MS / portTICK_RATE_MS);
    k_gpio_write(pin, 0);
}


void csp_server(void *p) {
    (void) p;

    portBASE_TYPE task_woken = pdFALSE;
    /* Create socket without any socket options */
    csp_socket_t *sock = csp_socket(CSP_SO_NONE);

    /* Bind all ports to socket */
    csp_bind(sock, CSP_ANY);

    /* Create 10 connections backlog queue */
    csp_listen(sock, 10);

    /* Pointer to current connection and packet */
    csp_conn_t *conn;
    csp_packet_t *packet;

    /* Process incoming connections */
    while (1) {

        printf("goodbye");

        //TODO: make to work with other UART on same bus (printf)
//        while (usart_messages_waiting(K_UART6))
//        {
//            buf = usart_getc();
//            /* send char pointer from UART to KISS interface */
//            csp_kiss_rx(&csp_if_kiss, (uint8_t*)&buf, 1, &task_woken);
//        }

        /* Wait for connection, 100 ms timeout */
        if ((conn = csp_accept(sock, 100)) == NULL)
            continue;

        /* Read packets. Timout is 100 ms */
        while ((packet = csp_read(conn, 100)) != NULL) {
            switch (csp_conn_dport(conn)) {
                case MY_PORT:
                    /* Process packet here */
                    blink(K_LED_GREEN);
                    csp_buffer_free(packet);
                    break;

                default:
                    /* Let the service handler reply pings, buffer use, etc. */
                    #ifdef TARGET_LIKE_MSP430
                    blink(K_LED_GREEN);
                    blink(K_LED_GREEN);
                    #else
                    blink(K_LED_BLUE);
                    #endif
                    csp_service_handler(conn, packet);
                    break;
            }
        }

        /* Close current connection, and handle next */
        csp_close(conn);

    }
}

void csp_client(void *p) {
    (void) p;
    csp_packet_t * packet;
    csp_conn_t * conn;
    portBASE_TYPE status;
    int signal;

    /**
     * Try data packet to server
     */

    while (1) {
        status = xQueueReceive(button_queue, &signal, portMAX_DELAY);
        if (status != pdTRUE) {
            continue;
        }

        /* Get packet buffer for data */
        packet = csp_buffer_get(100);
        if (packet == NULL) {
            /* Could not get buffer element */
            return;
        }

        /* Connect to host HOST, port PORT with regular UDP-like protocol and 1000 ms timeout */
        blink(K_LED_RED);
        conn = csp_connect(CSP_PRIO_NORM, MY_ADDRESS, MY_PORT, 1000, CSP_O_NONE);
        if (conn == NULL) {
            /* Connect failed */
            /* Remember to free packet buffer */
            csp_buffer_free(packet);
            return;
        }

        blink(K_LED_RED);
        /* Copy dummy data to packet */
        char *msg = "Hello WorldHello WorldHellolqkwjdqwkljdhqwkjhdqkwjhdqMITCH";
        strcpy((char *) packet->data, msg);

        /* Set packet length */
        packet->length = strlen(msg);

        /* Send packet */
        if (!csp_send(conn, packet, 1000)) {
            /* Send failed */
            csp_buffer_free(packet);
        }

        blink(K_LED_RED);
        /* Close connection */
        csp_close(conn);
    }
}

void task_button_press(void *p) {
   int signal = 1;

    while (1) {
        if (k_gpio_read(K_BUTTON_0)) {
            while (k_gpio_read(K_BUTTON_0))
                vTaskDelay(50 / portTICK_RATE_MS); /* Button Debounce Delay */
            while (!k_gpio_read(K_BUTTON_0))
                vTaskDelay(50 / portTICK_RATE_MS); /* Button Debounce Delay */

            blink(K_LED_RED);
            xQueueSendToBack(button_queue, &signal, 0); /* Send Message */
        }
        vTaskDelay(100 / portTICK_RATE_MS);
    }
}

int main(void)
{
    k_uart_console_init();

    #ifdef TARGET_LIKE_STM32
    k_gpio_init(K_LED_GREEN, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_ORANGE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_RED, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_BLUE, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_BUTTON_0, K_GPIO_INPUT, K_GPIO_PULL_NONE);
    #endif

    #ifdef TARGET_LIKE_MSP430
    k_gpio_init(K_LED_GREEN, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_LED_RED, K_GPIO_OUTPUT, K_GPIO_PULL_NONE);
    k_gpio_init(K_BUTTON_0, K_GPIO_INPUT, K_GPIO_PULL_UP);
    /* Stop the watchdog. */
    WDTCTL = WDTPW + WDTHOLD;

    __enable_interrupt();

    P2OUT = BIT1;
    #endif

    button_queue = xQueueCreate(10, sizeof(int));

    struct usart_conf conf;

    /* set the device in KISS / UART interface */
    char dev = (char)K_UART6;
    conf.device = &dev;
    usart_init(&conf);

    /* init kiss interface */
    csp_kiss_init(&csp_if_kiss, &csp_kiss_driver, usart_putc, usart_insert, "KISS");

    /* Setup callback from USART RX to KISS RS */
    void my_usart_rx(uint8_t * buf, int len, void * pxTaskWoken) {
        csp_kiss_rx(&csp_if_kiss, buf, len, pxTaskWoken);
    }
    usart_set_callback(my_usart_rx);

    /* csp buffer must be 256, or mtu in csp_iface must match */
    csp_buffer_init(5, 256);
    csp_init(MY_ADDRESS);
    /* set to route through KISS / UART */
    csp_route_set(MY_ADDRESS, &csp_if_kiss, CSP_NODE_MAC);
    csp_route_start_task(500, 1);

    xTaskCreate(csp_server, "CSPSRV", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(csp_client, "CSPCLI", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_button_press, "BUTTON", configMINIMAL_STACK_SIZE, NULL, 3, NULL);

    vTaskStartScheduler();

    while (1);

    return 0;
}
