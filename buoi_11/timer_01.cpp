#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "stdio.h"
#include "stdlib.h"
#include "CLCD_I2C.h"

typedef struct {uint8_t a, b, c, ans; uint16_t stt, kq1, kq2, kq3;} quizz_item;
typedef enum {START, END, PLAYING} game_status;
typedef enum {ANSWER, NOANSWER} ans_status;
typedef struct {uint8_t score; ans_status ans; game_status status;} program_status;

I2C_HandleTypeDef hi2c1;
CLCD_I2C_Name LCD1;
char lcd_text[16];
program_status mygame = {0,ANSWER,START};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);

TaskHandle_t button_handle, led_handle, quizz_handle, lcd_handle;
TimerHandle_t time_handle;

void         lcd_task(void * params);
void 		  button_task(void * params);
void         led_task(void * params);
void       quizz_task(void * params);
void timer_callback(TimerHandle_t xTimer);

QueueHandle_t     quizz_queue; // button_qhandle;
SemaphoreHandle_t scoreMutex;


int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init(&LCD1, &hi2c1, 0x4E, 16, 2);
	
	scoreMutex = xSemaphoreCreateMutex();
	time_handle = xTimerCreate("Timer", pdMS_TO_TICKS(2000),pdTRUE, NULL, timer_callback);
	xTimerStart(time_handle,0);
	
	xTaskCreate(lcd_task, "LCD task", 128, NULL, 2, &lcd_handle);
	xTaskCreate(quizz_task, "LCD task", 128, NULL, 3, &quizz_handle);	
	xTaskCreate(button_task, "Button task", 128, NULL, 2, &button_handle);
	xTaskCreate(led_task, "LED task", 128, NULL, 2, &led_handle);
	
	quizz_queue = xQueueCreate(10,sizeof(quizz_item));

	osKernelStart();
  
  while (1){}
}
void button_task(void * params){
	uint32_t notifyValue;
	quizz_item item;
	for(;;){
		xTaskNotifyWait(0,0xFFFFFFFF, &notifyValue,portMAX_DELAY);
		xSemaphoreTake(scoreMutex,portMAX_DELAY);
		if(mygame.status == PLAYING){
			if(xQueueReceive(quizz_queue, &item, 0) == pdTRUE){
				if(notifyValue & (1<<item.ans)){
					mygame.score ++;
					mygame.status = PLAYING;
				}else{
					mygame.status = END;
				}
				mygame.ans = ANSWER;
			}
		}else if(mygame.status == START){
			srand(HAL_GetTick());
			mygame.status = PLAYING;
		}else if(mygame.status == END){
			mygame.score = 0;
			mygame.ans = ANSWER;
			mygame.status = START;
		}
		xSemaphoreGive(scoreMutex);
	}
}
void timer_callback(TimerHandle_t xTimer){
	xTaskNotify(lcd_handle, 1<<0, eSetBits);
}
uint16_t minus(uint16_t x, uint16_t y){
	// max(0,x-y)
	if(x<y)
		return 0;
	else
		return x-y;
}
void quizz_task(void *params){
	static uint16_t stt = 0;
	uint8_t rand_num; uint16_t results;
	quizz_item item;
	for(;;){	
		item.stt = stt; stt++;
		item.a = (uint8_t) rand()%10;
		item.b = (uint8_t) rand()%10;
		item.c = (uint8_t) rand()%10;
		results = item.a + item.b + item.c;
		rand_num = rand()%3;
		if(rand_num == 0){
			item.ans = 0;
			item.kq1 = results;
			if(rand()%2 == 0){
				item.kq2 = results + 1 + rand()%9;
				item.kq3 = minus(results, 1 + rand()%9);
			}else{
				item.kq3 = results + 1 + rand()%9;
				item.kq2 = minus(results, 1 + rand()%9);
			}
		}else if(rand_num == 1){
			item.ans = 1;
			item.kq2 = results;
			if(rand()%2 == 0){
				item.kq1 = results + 1 + rand()%9;
				item.kq3 = minus(results, 1 + rand()%9);
			}else{
				item.kq3 = results + 1 + rand()%9;
				item.kq1 = minus(results, 1 + rand()%9);
			}
		}else{
			item.ans = 2;
			item.kq3 = results;
			if(rand()%2 == 0){
				item.kq1 = results + 1 + rand()%9;
				item.kq2 = minus(results, 1 + rand()%9);
			}else{
				item.kq2 = results + 1 + rand()%9;
				item.kq1 = minus(results, 1 + rand()%9);
			}
		}
		xQueueSend(quizz_queue, &item, portMAX_DELAY);
	}
}
void lcd_task(void* params){
	uint32_t notifyValue;	
	quizz_item item;
	uint8_t score_copy;
	game_status status_copy;
	ans_status ans_copy;
	for(;;){
		xTaskNotifyWait(0,0xFFFFFFFF,&notifyValue, portMAX_DELAY);
		xSemaphoreTake(scoreMutex, portMAX_DELAY);
		score_copy = mygame.score;
		ans_copy  = mygame.ans;
		if(ans_copy != ANSWER){
			mygame.status = END;
			status_copy = END;
		}
		status_copy = mygame.status;
		xSemaphoreGive(scoreMutex);
		
		if(status_copy == PLAYING){
			if(xQueuePeek(quizz_queue, &item, 0) == pdTRUE){
				CLCD_I2C_Clear(&LCD1);
				CLCD_I2C_SetCursor(&LCD1,0,0);
				sprintf(lcd_text,"Q%2d: %2d+%2d+%2d=?",item.stt,item.a,item.b,item.c);
				CLCD_I2C_WriteString(&LCD1,lcd_text);
				CLCD_I2C_SetCursor(&LCD1,0,1);
				sprintf(lcd_text,"%3d|  %2d %2d %2d",score_copy,item.kq1,item.kq2,item.kq3);
				CLCD_I2C_WriteString(&LCD1,lcd_text);
				xSemaphoreTake(scoreMutex, portMAX_DELAY);
				mygame.ans = NOANSWER;
				xSemaphoreGive(scoreMutex);
			}
		}else if(status_copy == END){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1,"YOU LOST | PLAY");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			sprintf(lcd_text,          "SCORE: %2d| AGAIN",score_copy);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}else if(status_copy == START){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1,"PRESS BUTTON");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			CLCD_I2C_WriteString(&LCD1,"TO START");
		}
	}
}
void led_task(void * params){
	uint32_t notifyValue;
	for(;;){
		xTaskNotifyWait(0, 0xFFFFFFFF, &notifyValue, portMAX_DELAY);
		if(notifyValue & (1<<0)){
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,GPIO_PIN_SET);
			vTaskDelay(pdMS_TO_TICKS(100));
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,GPIO_PIN_RESET);
		}
		else if(notifyValue & (1<<1)){
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_SET);
			vTaskDelay(pdMS_TO_TICKS(100));
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);
		}
		else if(notifyValue & (1<<2)){
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_SET);
			vTaskDelay(pdMS_TO_TICKS(100));
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_RESET);
		}
	}
}
void EXTI15_10_IRQHandler(void){
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_10)){    // EXTI->PR & GPIO_PIN_10
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);   // auto call	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_10);
	}
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14)){    // EXTI->PR & GPIO_PIN_14
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);   //__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);
	}
	if(__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_15)){   // EXTI->PR & GPIO_PIN_15
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);  // __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_15);
	}
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
//	buttonAction currAction;
	BaseType_t xHPTW = pdFALSE;
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	if (now - last_tick > pdMS_TO_TICKS(400)) // debounce 400ms
	{
		last_tick = now;
		if( GPIO_Pin == GPIO_PIN_14){       
			xTaskNotifyFromISR(led_handle, 1<<0, eSetBits, &xHPTW);
			xTaskNotifyFromISR(button_handle, 1<<0, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_10){
			xTaskNotifyFromISR(led_handle, 1<<1, eSetBits, &xHPTW);
			xTaskNotifyFromISR(button_handle, 1<<1, eSetBits, &xHPTW);
		}else if(GPIO_Pin == GPIO_PIN_15){
			xTaskNotifyFromISR(led_handle, 1<<2, eSetBits, &xHPTW);
			xTaskNotifyFromISR(button_handle, 1<<2, eSetBits, &xHPTW);
		}
//		xQueueSendFromISR(buttonQueue, &currAction, &xHPTW);
		portYIELD_FROM_ISR(xHPTW);
	}
}
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout){
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
	if( HAL_I2C_IsDeviceReady(hi2c, addr, trails, timeout) == HAL_OK){
		for(int i = 0;i<20;i++){
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			HAL_Delay(50);
		}
	}else{
		for(int i = 0;i<6;i++){
			HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
			HAL_Delay(500);
		}
	}
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}
void SystemClock_Config(void){
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
static void MX_I2C1_Init(void){
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}
static void MX_GPIO_Init(void){
  GPIO_InitTypeDef InitStruct = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();

	/*Configure GPIO pins : PB5 PB6 PB7 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);
  InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  InitStruct.Pull = GPIO_NOPULL;
  InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &InitStruct);
	
	/*Configure GPIO pins : PC13 */
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
	InitStruct.Pin = GPIO_PIN_13;
  InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  InitStruct.Pull = GPIO_NOPULL;
  InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &InitStruct);

	// EXTI PA10, PA15
	InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_15;
	InitStruct.Mode = GPIO_MODE_IT_FALLING;
	InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOA, &InitStruct);
	// EXTI PB14
	InitStruct.Pin = GPIO_PIN_14;
	HAL_GPIO_Init(GPIOB, &InitStruct);
	// Enable EXTI15_10
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}
void Error_Handler(void){
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line){}
#endif /* USE_FULL_ASSERT */
