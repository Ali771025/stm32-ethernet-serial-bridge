/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "platform_fault.h"
#include "eth_bringup_diag.h"
#include "eth_ping_probe.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
  (void)xTask;
  (void)pcTaskName;
  platform_fault_raise(PLATFORM_FAULT_STACK_OVERFLOW, __FILE__, (uint32_t)__LINE__);
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
  platform_fault_raise(PLATFORM_FAULT_MALLOC_FAILED, __FILE__, (uint32_t)__LINE__);
}
/* USER CODE END 5 */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

extern volatile uint32_t u8b_lwip_netif_ready;

volatile eth_bringup_diag_t u8b_eth_bringup_diag;

void eth_bringup_diag_poll(uint32_t now_ms)
{
  eth_bringup_diag_t sample;
  static uint32_t last_sample_ms;

  /* No LwIP or MDIO call: the Ethernet/link owners publish these counters.
   * Sampling every 250 ms keeps the default task's work bounded. */
  if ((uint32_t)(now_ms - last_sample_ms) < 250U)
  {
    return;
  }
  last_sample_ms = now_ms;
  sample.sample_ms = now_ms;
  sample.samples = u8b_eth_bringup_diag.samples + 1U;
  sample.lwip_netif_ready = u8b_lwip_netif_ready;
  sample.phy_init_attempts = u8b_eth_ping_probe.phy_init_attempts;
  sample.phy_init_status = u8b_eth_ping_probe.phy_init_status;
  sample.phy_id1 = u8b_eth_ping_probe.mdio_id1;
  sample.phy_id2 = u8b_eth_ping_probe.mdio_id2;
  sample.phy_link_state = u8b_eth_ping_probe.phy_raw_link_state;
  sample.eth_start_attempts = u8b_eth_ping_probe.eth_start_attempts;
  sample.eth_start_errors = u8b_eth_ping_probe.eth_start_errors;
  sample.eth_irqs = u8b_eth_ping_probe.eth_irqs;
  sample.rx_frames = u8b_eth_ping_probe.rx_frames;
  sample.tx_attempts = u8b_eth_ping_probe.tx_attempts;
  sample.tx_errors = u8b_eth_ping_probe.tx_errors;
  sample.stage = (uint32_t)eth_bringup_classify(&sample);

  u8b_eth_bringup_diag.sample_ms = sample.sample_ms;
  u8b_eth_bringup_diag.samples = sample.samples;
  u8b_eth_bringup_diag.lwip_netif_ready = sample.lwip_netif_ready;
  u8b_eth_bringup_diag.phy_init_attempts = sample.phy_init_attempts;
  u8b_eth_bringup_diag.phy_init_status = sample.phy_init_status;
  u8b_eth_bringup_diag.phy_id1 = sample.phy_id1;
  u8b_eth_bringup_diag.phy_id2 = sample.phy_id2;
  u8b_eth_bringup_diag.phy_link_state = sample.phy_link_state;
  u8b_eth_bringup_diag.eth_start_attempts = sample.eth_start_attempts;
  u8b_eth_bringup_diag.eth_start_errors = sample.eth_start_errors;
  u8b_eth_bringup_diag.eth_irqs = sample.eth_irqs;
  u8b_eth_bringup_diag.rx_frames = sample.rx_frames;
  u8b_eth_bringup_diag.tx_attempts = sample.tx_attempts;
  u8b_eth_bringup_diag.tx_errors = sample.tx_errors;
  u8b_eth_bringup_diag.stage = sample.stage;
}

/* USER CODE END Application */

