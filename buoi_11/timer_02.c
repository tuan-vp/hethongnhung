#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "stdio.h"
#include "stdlib.h"
#include "CLCD_I2C.h"

typedef enum {ANSWERED, NOANSWER, NONE} gameANS;
typedef enum {START, WAITING, PLAYING, NEXTGAME, FINISH, END} gameSTATE;
typedef struct {uint32_t level, levelup_counter; gameSTATE state; gameANS ans;} playerSTATE;
typedef struct {uint32_t score, bonus;} lcd_item;

I2C_HandleTypeDef hi2c1;
CLCD_I2C_Name LCD1;
char lcd_text[20];
playerSTATE mygame = {0, 0, START, ANSWERED};
lcd_item game_lcd = {0,1};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);

TaskHandle_t button_handle, led_handle, quizz_handle, lcd_handle, pc13_handle;
TimerHandle_t pc13_timer_hd, button_timer_hd;

void         task_lcd(void * params);
void 		  task_button(void * params);
void         task_led(void * params);
void       task_quizz(void * params);
void timer_debounce_callback(TimerHandle_t xTimer);
void   timer_button_callback(TimerHandle_t xTimer);
void     timer_PC13_callback(TimerHandle_t xTimer);

QueueHandle_t     qled_hd, qbutton_hd, qlcd_hd; // button_qhandle;
SemaphoreHandle_t myMutex;


int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init(&LCD1, &hi2c1, 0x4E, 16, 2);
	
	myMutex = xSemaphoreCreateMutex();
	button_timer_hd = xTimerCreate("Press button Timer", 
					pdMS_TO_TICKS(1000),pdFALSE, NULL, timer_button_callback);
	pc13_timer_hd = xTimerCreate("PC13 Timer", 
					pdMS_TO_TICKS(500),pdTRUE, NULL, timer_PC13_callback);
	xTimerStart(pc13_timer_hd, 0);
	
	// 5 TASK with 128 => NOT ENOUGH RAM RESOURCE
	xTaskCreate(task_lcd, "LCD task",       128, NULL, 2, &lcd_handle);
	xTaskCreate(task_quizz, "Quizz task",      64, NULL, 3, &quizz_handle); // 128 -> 64	
	xTaskCreate(task_button, "Button task", 128, NULL, 2, &button_handle);
	xTaskCreate(task_led, "LED task",        64, NULL, 2, &led_handle);   // 128 -> 64
	
	qled_hd = xQueueCreate(10,sizeof(uint32_t));
	qbutton_hd = xQueueCreate(10,sizeof(uint32_t));
	
	osKernelStart();
  
  while (1){}
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

uint32_t set_time_each_level(uint32_t _level){
	if(_level >= 8) return 2500;
	else if(_level >= 6) return 2300;
	else if(_level >= 4) return 2100;
	else return 2000;	
}

void timer_button_callback(TimerHandle_t xTimer){
	BaseType_t xHPTW = pdFALSE;
	xSemaphoreTakeFromISR(myMutex, &xHPTW);
	mygame.state = END;
	xSemaphoreGiveFromISR(myMutex, &xHPTW);
	xTaskNotify(lcd_handle,1<<0,eSetBits);
	portYIELD_FROM_ISR(xHPTW);
}

void timer_PC13_callback(TimerHandle_t xTimer){
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}

void task_button(void * params){
	uint32_t notifyValue, 
	         _level, _levelup_counter, _state,
	         item, _item, _score, _bonus;
	gameANS _ans;

	for(;;){
		xTaskNotifyWait(0,0xFFFFFFFF, &notifyValue,portMAX_DELAY);
		xSemaphoreTake(myMutex, portMAX_DELAY);
		_level = mygame.level;
		_levelup_counter = mygame.levelup_counter;
		_state = mygame.state;
		_ans = mygame.ans;
		_score  = game_lcd.score;
		_bonus  = game_lcd.bonus;
		xSemaphoreGive(myMutex);
		if(_state == PLAYING){
			xTimerReset(button_timer_hd, 0); // RESET TIMER HERE
			xQueueReceive(qbutton_hd, &_item, 0);
			if(notifyValue      == (1<<0)) item = 0;
			else if(notifyValue == (1<<1)) item = 1;
			else if(notifyValue == (1<<2)) item = 2;
			if(_item == item){
				_bonus = _level+1;
				_score += _bonus;
				_levelup_counter ++;
				if(_levelup_counter > _level){
					xTimerStop(button_timer_hd, 0); // STOP TIMER HERE
					_ans = ANSWERED;
					_level ++;
					_levelup_counter = 0;
					xSemaphoreTake(myMutex, portMAX_DELAY);
					mygame.state = NEXTGAME;
					xSemaphoreGive(myMutex);
				}
			}else{
				xTimerStop(button_timer_hd, 0); // STOP TIMER HERE
				xQueueReset(qled_hd);
				xQueueReset(qbutton_hd);
				_levelup_counter = 0;
				xSemaphoreTake(myMutex, portMAX_DELAY);
				mygame.state = END;
				xSemaphoreGive(myMutex);
			}
		}else if(_state == START){
			_score = 0;
			_level = 0;
			_bonus = _level+1;
			_levelup_counter = 0;
			_ans = NOANSWER;
			xSemaphoreTake(myMutex, portMAX_DELAY);
			mygame.state = NEXTGAME;
			xSemaphoreGive(myMutex);
		}else if(_state == END){
			xSemaphoreTake(myMutex, portMAX_DELAY);
			mygame.state = START;
			xSemaphoreGive(myMutex);
		}else if(_state == NEXTGAME){
			uint32_t time_ms = set_time_each_level(_level);
			xTimerChangePeriod(button_timer_hd, pdMS_TO_TICKS(time_ms), 0); //
			xSemaphoreTake(myMutex, portMAX_DELAY);
			mygame.state = PLAYING;
			xSemaphoreGive(myMutex);
			xTaskNotify(led_handle,1<<0,eSetBits);
		}else{
			// WAITING or FINISH
		}
		xSemaphoreTake(myMutex, portMAX_DELAY);
		mygame.ans = 	_ans;
		mygame.level = _level;
		mygame.levelup_counter = _levelup_counter;
		game_lcd.score = _score;
		game_lcd.bonus = _bonus;
		xSemaphoreGive(myMutex);
		xTaskNotify(lcd_handle,1<<0,eSetBits);
	}
}

void task_quizz(void *params){
	uint32_t item;
	for(;;){
		item = rand()%3;
		xQueueSend(qled_hd, &item,portMAX_DELAY);
	}
}

void task_lcd(void* params){
	uint32_t notifyValue,
					 _level, _state, _score, _bonus;
	for(;;){
		xSemaphoreTake(myMutex, portMAX_DELAY);
		_level  = mygame.level;
		_state = mygame.state;
		_score  = game_lcd.score;
		_bonus  = game_lcd.bonus;
		xSemaphoreGive(myMutex);
		if(_state == PLAYING){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1, 0,0);
			sprintf(lcd_text, "Bonus: +%d", _bonus);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
			CLCD_I2C_SetCursor(&LCD1, 0,1);
			sprintf(lcd_text, "Score: %2d", _score);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}else if(_state == END){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1,"------LOST------");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			sprintf(lcd_text,          "Score: %2d",_score);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}else if(_state == START){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1," PRESS BUTTONS");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			CLCD_I2C_WriteString(&LCD1," TO START GAME");
		}else if(_state == NEXTGAME){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1,"     LEVEL:");
			CLCD_I2C_SetCursor(&LCD1,0,1);
			sprintf(lcd_text, "      %2d",_level);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}else{
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1,0,0);
			CLCD_I2C_WriteString(&LCD1,"   WAITING...");
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}
		xTaskNotifyWait(0,0xFFFFFFFF,&notifyValue,portMAX_DELAY);
	}
}

void task_led(void * params){
	uint32_t notifyValue, _level, curLED;
	gameSTATE _state;
	uint16_t curPIN = GPIO_PIN_5;
	for(;;){
		xTaskNotifyWait(0, 0xFFFFFFFF, &notifyValue, portMAX_DELAY);
		xSemaphoreTake(myMutex, portMAX_DELAY);
		_level = mygame.level;
		_state = mygame.state;
		mygame.state = WAITING;
		xSemaphoreGive(myMutex);
		xTaskNotify(lcd_handle, 1<<0, eSetBits);
		for(int i=0;i<_level+1;i++){
			if(xQueueReceive(qled_hd, &curLED, 0)==pdFALSE){
				HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
				HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_6);
				HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_7);
				vTaskDelay(pdMS_TO_TICKS(500));
			}else{
				xQueueSend(qbutton_hd, &curLED, 0);
				if(curLED == 0) curPIN = GPIO_PIN_5;
				else if(curLED == 1) curPIN = GPIO_PIN_6;
				else if(curLED == 2) curPIN = GPIO_PIN_7;
				vTaskDelay(pdMS_TO_TICKS(500));
				HAL_GPIO_WritePin(GPIOB, curPIN, GPIO_PIN_SET);
				vTaskDelay(pdMS_TO_TICKS(500));
				HAL_GPIO_WritePin(GPIOB, curPIN, GPIO_PIN_RESET);
			}
		}
		xSemaphoreTake(myMutex, portMAX_DELAY);
		mygame.ans = NOANSWER;
		mygame.state = _state;
		xSemaphoreGive(myMutex);
		xTaskNotify(lcd_handle, 1<<0, eSetBits);
		xTimerReset(button_timer_hd,0); // RESET & START TIMER
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
	BaseType_t xHPTW1 = pdFALSE, xHPTW2 = pdFALSE;
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	if (now - last_tick > pdMS_TO_TICKS(150)) // debounce 150ms
	{
		last_tick = now;
		if( GPIO_Pin == GPIO_PIN_14){       
			xTaskNotifyFromISR(button_handle, 1<<0, eSetBits, &xHPTW2);
		}else if(GPIO_Pin == GPIO_PIN_10){
			xTaskNotifyFromISR(button_handle, 1<<1, eSetBits, &xHPTW2);
		}else if(GPIO_Pin == GPIO_PIN_15){
			xTaskNotifyFromISR(button_handle, 1<<2, eSetBits, &xHPTW2);
		}
		portYIELD_FROM_ISR(xHPTW1 || xHPTW2);
	}
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
