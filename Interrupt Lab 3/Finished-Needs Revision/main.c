#include "FreeRTOS.h"
#include "board_io.h"
#include "common_macros.h"
#include "gpio_isr.h"
#include "gpio_lab.h"
#include "lpc_peripherals.h"
#include "periodic_scheduler.h"
#include "semphr.h"
#include "sj2_cli.h"
#include "task.h"
#include <lpc40xx.h>
#include <stdbool.h>
#include <stdio.h>

// 'static' to make these functions 'private' to this file
static void create_blinky_tasks(void);
static void create_uart_task(void);
static void blink_task(void *params);
static void uart_task(void *params);
static void choose_pins_as_io_pins(void);
static void initialize_all_led(void);
static void gpio_interrupt(void);

// Interrupt lab exclusive
static void interrupt_part_0(void);
static void interrupt_w_semaphore(void);
static void configure_your_gpio_interrupt(void);
static void sleep_on_sem_task(void *p);

static SemaphoreHandle_t switch_press_indication;
static SemaphoreHandle_t switch_pressed_signal;
static void pin30_isr(void);
static void pin29_isr(void);

typedef struct {
  uint8_t port;
  uint8_t pin;
} port_pin_s;

static port_pin_s led[4] = {{2, 3}, {1, 26}, {1, 24}, {1, 18}};

static void initialize_all_led(void) {
  gpio0__set_high(led[0].port, led[0].pin);
  gpio0__set_high(led[1].port, led[1].pin);
  gpio0__set_high(led[2].port, led[2].pin);
  gpio0__set_high(led[3].port, led[3].pin);
}

static void configure_your_gpio_interrupt(void) {
  static port_pin_s switch0 = {0, 30};
  gpio0__set_as_input(switch0.port, switch0.pin);
  LPC_GPIOINT->IO0IntEnF |= (1 << switch0.pin); // Might need to be (1 << 30);
  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio_interrupt, "unused");
  NVIC_EnableIRQ(GPIO_IRQn);
}

static void interrupt_part_0(void) {

  configure_your_gpio_interrupt();

  while (1) {
    delay__ms(1000);
    gpio0__set_low(led[0].port, led[0].pin);
    delay__ms(1000);
    gpio0__set_high(led[0].port, led[0].pin);
  }
}

static void interrupt_w_semaphore(void) {
  switch_pressed_signal = xSemaphoreCreateBinary();
  configure_your_gpio_interrupt();
}

static void clear_gpio_interrupt(void) { LPC_GPIOINT->IO0IntClr |= (1 << 30); }

static void sleep_on_sem_task(void *p) {
  while (1) {
    // Use xSemaphoreTake with forever delay and blink an LED when you get the signal
    if (xSemaphoreTake(switch_pressed_signal, 1000)) {
      gpio0__set_low(led[1].port, led[1].pin);
      // delay__ms(200);
      vTaskDelay(200);
      gpio0__set_high(led[1].port, led[1].pin);
      vTaskDelay(200);
      // delay__ms(200);
    }
  }
}

static void pin29_isr(void) {
  fprintf(stderr, "Interrupt Pin 29 Received\n");
  xSemaphoreGiveFromISR(switch_pressed_signal, NULL);
}
static void pin30_isr(void) { fprintf(stderr, "Interrupt Pin 30 Received\n"); }

void main(void) {
  initialize_all_led();

  switch_pressed_signal = xSemaphoreCreateBinary();

  static port_pin_s switch0 = {0, 29};
  static port_pin_s switch1 = {0, 30};
  gpio0__set_as_input(switch0.port, switch0.pin);
  gpio0__set_as_input(switch1.port, switch1.pin);
  gpio0__attach_interrupt(29, GPIO_INTR__RISING_EDGE, pin29_isr);
  gpio0__attach_interrupt(30, GPIO_INTR__FALLING_EDGE, pin30_isr);
  NVIC_EnableIRQ(GPIO_IRQn);
  // gpio0__interrupt_dispatcher();

  xTaskCreate(sleep_on_sem_task, "sem", (512U * 4) / sizeof(void *), NULL, PRIORITY_LOW, NULL);
  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio0__interrupt_dispatcher, NULL);

  vTaskStartScheduler(); // This function never returns unless RTOS scheduler runs out of memory and fails
  return 0;
}

static void gpio_interrupt(void) {
  fprintf(stderr, "ISR Entry\n");
  xSemaphoreGiveFromISR(switch_pressed_signal, NULL);
  clear_gpio_interrupt();
}

/* ** PART 0 **
static void gpio_interrupt(void) {

  LPC_GPIOINT->IO0IntClr = (1 << 30);

  fprintf(stderr, "Int detected\n");
}*/

static void create_blinky_tasks(void) {
  /**
   * Use '#if (1)' if you wish to observe how two tasks can blink LEDs
   * Use '#if (0)' if you wish to use the 'periodic_scheduler.h' that will spawn 4 periodic tasks, one for each LED
   */
#if (1)
  // These variables should not go out of scope because the 'blink_task' will reference this memory
  static gpio_s led0, led1;

  led0 = board_io__get_led0();
  led1 = board_io__get_led1();

  xTaskCreate(blink_task, "led0", configMINIMAL_STACK_SIZE, (void *)&led0, PRIORITY_LOW, NULL);
  xTaskCreate(blink_task, "led1", configMINIMAL_STACK_SIZE, (void *)&led1, PRIORITY_LOW, NULL);
#else
  const bool run_1000hz = true;
  const size_t stack_size_bytes = 2048 / sizeof(void *); // RTOS stack size is in terms of 32-bits for ARM M4 32-bit CPU
  periodic_scheduler__initialize(stack_size_bytes, !run_1000hz); // Assuming we do not need the high rate 1000Hz task
  UNUSED(blink_task);
#endif
}

static void create_uart_task(void) {
  // It is advised to either run the uart_task, or the SJ2 command-line (CLI), but not both
  // Change '#if (0)' to '#if (1)' and vice versa to try it out
#if (0)
  // printf() takes more stack space, size this tasks' stack higher
  xTaskCreate(uart_task, "uart", (512U * 8) / sizeof(void *), NULL, PRIORITY_LOW, NULL);
#else
  sj2_cli__init();
  UNUSED(uart_task); // uart_task is un-used in if we are doing cli init()
#endif
}

static void blink_task(void *params) {
  const gpio_s led = *((gpio_s *)params); // Parameter was input while calling xTaskCreate()

  // Warning: This task starts with very minimal stack, so do not use printf() API here to avoid stack overflow
  while (true) {
    gpio__toggle(led);
    vTaskDelay(500);
  }
}

// This sends periodic messages over printf() which uses system_calls.c to send them to UART0
static void uart_task(void *params) {
  TickType_t previous_tick = 0;
  TickType_t ticks = 0;

  while (true) {
    // This loop will repeat at precise task delay, even if the logic below takes variable amount of ticks
    vTaskDelayUntil(&previous_tick, 2000);

    /* Calls to fprintf(stderr, ...) uses polled UART driver, so this entire output will be fully
     * sent out before this function returns. See system_calls.c for actual implementation.
     *
     * Use this style print for:
     *  - Interrupts because you cannot use printf() inside an ISR
     *    This is because regular printf() leads down to xQueueSend() that might block
     *    but you cannot block inside an ISR hence the system might crash
     *  - During debugging in case system crashes before all output of printf() is sent
     */
    ticks = xTaskGetTickCount();
    fprintf(stderr, "%u: This is a polled version of printf used for debugging ... finished in", (unsigned)ticks);
    fprintf(stderr, " %lu ticks\n", (xTaskGetTickCount() - ticks));

    /* This deposits data to an outgoing queue and doesn't block the CPU
     * Data will be sent later, but this function would return earlier
     */
    ticks = xTaskGetTickCount();
    printf("This is a more efficient printf ... finished in");
    printf(" %lu ticks\n\n", (xTaskGetTickCount() - ticks));
  }
}
