#include "main.h"
#include "cmsis_os.h"

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
void HTN_B05_LedPB05(void * arg);
void HTN_B05_LedPB06(void * arg);
void HTN_B05_LedPB07(void * arg);
void HTN_B05_LedPC13(void * arg);

TaskHandle_t LedPC13 = NULL, LedPB05 = NULL, LedPB06 = NULL, LedPB07 = NULL;
static uint16_t toggle_times = 0;

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
	
	xTaskCreate(	HTN_B05_LedPC13,"Led PC13",128, NULL,
								tskIDLE_PRIORITY + 1, &LedPC13);
  xTaskCreate(	HTN_B05_LedPB05,"Led PB05",128, NULL,
								tskIDLE_PRIORITY + 1, &LedPB05);
  xTaskCreate(	HTN_B05_LedPB06,"Led PB06",128, NULL,
								tskIDLE_PRIORITY + 1, &LedPB06);
  xTaskCreate(	HTN_B05_LedPB07,"Led PB07",128, NULL,
								tskIDLE_PRIORITY + 1, &LedPB07);
	vTaskSuspend(LedPB06);
	vTaskSuspend(LedPB07);
	vTaskStartScheduler();
  while (1)
  {
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin =GPIO_PIN_5|GPIO_PIN_6| GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	
	GPIO_InitStruct.Pin = GPIO_PIN_10;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	
  // EXTI10 in EXTI15_10
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void HTN_B05_LedPB05(void * argument)
{
	static uint16_t count_PB05 = 0;
  for(;;)
  {
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
    vTaskDelay(pdMS_TO_TICKS(500));
		count_PB05 ++;
		if(count_PB05 > toggle_times){
			count_PB05 = 0;
			vTaskResume(LedPB06);
			vTaskSuspend(NULL);
		}
  }
}
void HTN_B05_LedPB06(void * argument)
{
	static uint16_t count_PB06 = 0;
  for(;;)
  {
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_6);
    vTaskDelay(pdMS_TO_TICKS(500));
		count_PB06 ++;
		if(count_PB06 > toggle_times){
			count_PB06 = 0;
			vTaskResume(LedPB07);
			vTaskSuspend(NULL);
		}
  }
}
void HTN_B05_LedPB07(void * argument)
{
	static uint16_t count_PB07 = 0;
  for(;;)
  {
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_7);
    vTaskDelay(pdMS_TO_TICKS(500));
		count_PB07 ++;
		if(count_PB07 > toggle_times){
			count_PB07 = 0;
			vTaskResume(LedPB05);
			vTaskSuspend(NULL);
	}
  }
}
void HTN_B05_LedPC13(void * argument)
{
  for(;;)
  {
		HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    vTaskDelay(pdMS_TO_TICKS(500));
		if (ulTaskNotifyTake(pdTRUE, 0))
		{
				// suspend itself
				vTaskSuspend(NULL);
		}		
  }
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{		
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	static uint16_t suspended = 0;

	if (GPIO_Pin == GPIO_PIN_10)
	{
			if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 200ms
			{	
				if(suspended == 0){
					vTaskNotifyGiveFromISR(LedPC13,&xHigherPriorityTaskWoken);
					portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
					suspended = 1;
				}else if(suspended == 1){
					xTaskResumeFromISR(LedPC13);
					suspended = 0;
				}
				last_tick = now;
			}
	}
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
