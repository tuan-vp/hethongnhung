#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "stdio.h"
#include "stdlib.h"
#include "CLCD_I2C.h"
#include "utils.h"
/********************************************
Check/Add 
#define configUSE_COUNTING_SEMAPHORES     1
#define configUSE_MUTEXES                 1
#define configUSE_TIMERS                  1
in cmsis_os.h > FreeRTOS.h > FreeRTOSConfig.h
********************************************/

typedef enum {START, WAITING, PLAYING, NEXTGAME, FINISH, END} gameSTATE;
typedef struct {uint32_t level, score; gameSTATE state;} playerSTATE;
typedef enum {TO_LEFT, TO_RIGHT} direction;
typedef struct{BaseType_t need_Clear; uint32_t index;} colCLEAR;

uint8_t allLane[4][13] = {{0,0,0,0,0,0,0,0,0,0,0,0,0},
													{3,0,0,0,0,0,0,0,0,0,0,0,0},
													{0,0,0,0,0,0,0,0,0,0,0,0,0},
													{0,0,0,0,0,0,0,0,0,0,0,0,0}};

uint8_t Tlane[13] = { __u,_n_,_n_,_n_,_n_,_n_,_n_,
											_n_,_n_,_n_,_n_,_n_,_n_};
uint8_t Blane[13] = { _n_,_n_,_n_,_n_,_n_,_n_,_n_,
											_n_,_n_,_n_,_n_,_n_,_n_};
I2C_HandleTypeDef hi2c1;
CLCD_I2C_Name LCD1;
char lcd_text[20];
playerSTATE mygame = {0, 0, PLAYING};
uint32_t bottom_block = 13;
colCLEAR isclear;

static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void SystemClock_Config(void);
void task_lcd(void * params);
void task_button(void * params);
void task_bullet(void *params);
void task_quiz(void * params);
void timer_bult_callback(TimerHandle_t xTimer);
void timer_quiz_callback(TimerHandle_t xTimer);
void timer_PC13_callback(TimerHandle_t xTimer);
void timer_PB05_callback(TimerHandle_t xTimer);
void check_LCD_address(I2C_HandleTypeDef *hi2c, const uint8_t addr, int trails, int timeout);

TaskHandle_t       button_handle,bullet_handle,  quiz_handle, lcd_handle;
TimerHandle_t      pc13_timer_hd, button_timer_hd, quiz_timer_hd, bult_timer_hd;
QueueHandle_t      qbutton_hd;
SemaphoreHandle_t  myMutex, bottom_block_Mutex, stateMutex, taskBult_sem, taskQuiz_sem, taskLCD_sem;

int main(void){
  HAL_Init(); 
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
	
	check_LCD_address(&hi2c1, 0x4E, 3, 100);
	CLCD_I2C_Init(&LCD1, &hi2c1, 0x4E, 16, 2);
	
	bottom_block_Mutex = xSemaphoreCreateMutex(); 
	myMutex = xSemaphoreCreateMutex();
	stateMutex = xSemaphoreCreateMutex();
	taskBult_sem = xSemaphoreCreateCounting(10,0);
	taskQuiz_sem = xSemaphoreCreateCounting(10,0);
	taskLCD_sem = xSemaphoreCreateBinary();
	
	bult_timer_hd = xTimerCreate("Bullet timer",
									pdMS_TO_TICKS(150),pdTRUE, 
									NULL, timer_bult_callback);
	quiz_timer_hd = xTimerCreate("quizz timer", 
									pdMS_TO_TICKS(2000),pdTRUE, 
									NULL, timer_quiz_callback);
	pc13_timer_hd = xTimerCreate("PC13 Timer", 
									pdMS_TO_TICKS(500),pdTRUE, 
									NULL, timer_PC13_callback);
	xTimerStart(bult_timer_hd, 0);
	xTimerStart(quiz_timer_hd, 0);
	xTimerStart(pc13_timer_hd, 0);
	
	xTaskCreate(task_lcd, "LCD task",       128, NULL, 2, &lcd_handle);
	xTaskCreate(task_button, "Button task", 64, NULL, 2, &button_handle);
	xTaskCreate(task_bullet, "Bullet task", 128, NULL, 2, &bullet_handle);
	xTaskCreate(task_quiz, "task Quiz", 128, NULL, 2, &quiz_handle);
	
	qbutton_hd = xQueueCreate(10,sizeof(uint32_t));
	
	osKernelStart();
  
  while (1){	}
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

uint32_t set_period_level(uint32_t _level){
	if(_level >= 14) return 200;
	else if(_level >= 12) return 250;
	else if(_level >= 10) return 300;
	else if(_level >= 8) return 400;
	else if(_level >= 6) return 600;
	else if(_level >= 4) return 800;
	else return 1000;	
}
uint32_t set_level(uint32_t _score){
	return _score/5;	
}
void timer_PC13_callback(TimerHandle_t xTimer){
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}
void timer_PB05_callback(TimerHandle_t xTimer){
	HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);
}
void timer_bult_callback(TimerHandle_t xTimer){
	xSemaphoreGive(taskBult_sem);
}
void timer_quiz_callback(TimerHandle_t xTimer){
	xSemaphoreGive(taskQuiz_sem);
}
void gamemap_translator(){
	// take mutex allLane
	for(int i=1;i<13;i++){
		switch(allLane[0][i]){
			case 0: 
				switch(allLane[1][i]){
					case 0: Tlane[i] = _n_; break;
					case 1: Tlane[i] = __b; break;
					case 2: Tlane[i] = __o; break;
				}
				break;
			case 1:
				switch(allLane[1][i]){
					case 0: Tlane[i] = b__; break;
					case 2: Tlane[i] = b_o; break;
				}
				break;
			case 2: 
				switch(allLane[1][i]){
					case 0: Tlane[i] = o__; break;
					case 1: Tlane[i] = o_b; break;
					case 2: Tlane[i] = o_o; break;
				}
				break;
		}
	}
	for(int i=1;i<13;i++){
		switch(allLane[2][i]){
			case 0: 
				switch(allLane[3][i]){
					case 0: Blane[i] = _n_; break;
					case 1: Blane[i] = __b; break;
					case 2: Blane[i] = __o; break;
				}
				break;
			case 1:
				switch(allLane[3][i]){
					case 0: Blane[i] = b__; break;
					case 1: break;
					case 2: Blane[i] = b_o; break;
				}
				break;
			case 2: 
				switch(allLane[3][i]){
					case 0: Blane[i] = o__; break;
					case 1: Blane[i] = o_b; break;
					case 2: Blane[i] = o_o; break;
				}
				break;
		}
	}
	// give mutex allLane
}
void task_bullet(void *params){
	BaseType_t need_CLEAR = pdFALSE;
	uint32_t   clear_cols = 0;
	for(;;){
		xSemaphoreTake(taskBult_sem, portMAX_DELAY);
		xSemaphoreTake(stateMutex, portMAX_DELAY);
		xSemaphoreTake(myMutex, portMAX_DELAY);
		xSemaphoreTake(bottom_block_Mutex, portMAX_DELAY);
		// Clear cols determined at previous iteration
		uint8_t offset = 1;
		for(int j = 12; j > 0; j--){
			if(  allLane[0][j] == 2 && allLane[1][j] == 2 
				 &&allLane[2][j] == 2 && allLane[3][j] == 2){
				for(int i=0; i<4; i++){
					for(int k=j;k>0;k--){
						allLane[i][k] = allLane[i][k-1];
					}
				}
			}
		}
		// update bullet for Top Lane
		for(int i=0;i<4;i++){
			for(int j=12;j>1;j--){
				switch(allLane[i][j-1]){
					case 0: break;
					case 1:
						switch(allLane[i][j]){
							case 0: case 1: allLane[i][j-1] = 0; allLane[i][j] = 1; break;
							case 2: allLane[i][j-1] = 2; allLane[i][j] = 2; break;
						}
						break;
					case 2: break;
				}
			}
		}
		gamemap_translator();
		xSemaphoreGive(bottom_block_Mutex);
		xSemaphoreGive(myMutex);
		xSemaphoreGive(stateMutex);
		xSemaphoreGive(taskLCD_sem);
	}
}

void task_quiz(void * params){
	uint32_t item = 0;
	uint8_t nTlane, nBlane, TEMPlane;
	for(;;){
		xSemaphoreTake(taskQuiz_sem,portMAX_DELAY);
		xSemaphoreTake(stateMutex, portMAX_DELAY);
		xSemaphoreTake(myMutex, portMAX_DELAY);
		xSemaphoreTake(bottom_block_Mutex, portMAX_DELAY);
		if(  allLane[0][1] == 2 || allLane[1][1] == 2 
			|| allLane[2][1] == 2  ||allLane[3][1] == 2)
		{mygame.state = END;}
		if(mygame.state == END){ 
			xTimerStop(quiz_timer_hd,0);
			xTimerStop(bult_timer_hd,0);
		}
		// gen new blocks
		item = rand()%10; // 4:3block; 6:2block; => 10 case
		uint8_t newCol[4];
		newCol[0]=0; newCol[1]=0; newCol[2]=0; newCol[3]=0;
		switch(item){	
			case 0:  newCol[1]=2; newCol[2]=2; newCol[3]=2; break;
			case 1:  newCol[0]=2; newCol[2]=2; newCol[3]=2; break;
			case 2:  newCol[0]=2; newCol[1]=2; newCol[3]=2; break;
			case 3:  newCol[0]=2; newCol[1]=2; newCol[2]=2; break;
			case 4:  newCol[0]=2; newCol[1]=2; break;
			case 5:  newCol[0]=2; newCol[2]=2; break;
			case 6:  newCol[0]=2; newCol[3]=2; break;
			case 7:  newCol[1]=2; newCol[2]=2; break;
			case 8:  newCol[1]=2; newCol[3]=2; break;
			case 9:  newCol[2]=2; newCol[3]=2; break;

		}	
		for(int i=0; i<4; i++){
			for(int j=12;j>0;j--){
				uint8_t temp = allLane[i][j];
				allLane[i][j] = newCol[i];
				if(temp == 1 && newCol[i] == 2)
					newCol[i] = 2;
				else
					newCol[i] = temp;
			}
		}
		gamemap_translator();
		xSemaphoreGive(bottom_block_Mutex);
		xSemaphoreGive(myMutex);
		xSemaphoreGive(stateMutex);
		xSemaphoreGive(taskLCD_sem);
	}
}

void task_lcd(void* params){
	CLCD_I2C_CreateChar(&LCD1, u__, Tmycar_Bempty);
	CLCD_I2C_CreateChar(&LCD1, __u, Tempty_Bmycar);
	CLCD_I2C_CreateChar(&LCD1, o_o, Tbrick_Bbrick);
	CLCD_I2C_CreateChar(&LCD1, o_b, Tbrick_Bullet);
	CLCD_I2C_CreateChar(&LCD1, b_o, Bullet_Bbrick);
	CLCD_I2C_CreateChar(&LCD1, __b, Tempty_Bullet);
	CLCD_I2C_CreateChar(&LCD1, b__, Bullet_Bempty);
	CLCD_I2C_CreateChar(&LCD1, bbb, Bwalls_Bwalls);
	//								 #define o__  0xDF
	//								 #define __o  0xA1
	uint32_t _level, _score; 
	gameSTATE _state;
	for(;;)
	{
		xSemaphoreTake(taskLCD_sem, portMAX_DELAY);
		xSemaphoreTake(stateMutex, portMAX_DELAY);
		_level = mygame.level;
		_score = mygame.score;
		_state = mygame.state;
		xSemaphoreGive(stateMutex);
		if(_state == START){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1, 0,0);
			CLCD_I2C_WriteString(&LCD1,"  PRESS BUTTON");
			CLCD_I2C_SetCursor(&LCD1, 0,1);
			CLCD_I2C_WriteString(&LCD1,"  TO PLAY GAME");
		}else if(_state == END){
			CLCD_I2C_Clear(&LCD1);
			CLCD_I2C_SetCursor(&LCD1, 0,0);
			CLCD_I2C_WriteString(&LCD1,"      END");
			sprintf(lcd_text, "    score: %d", _score);
			CLCD_I2C_SetCursor(&LCD1, 0,1);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}else if(_state == PLAYING){
			CLCD_I2C_SetCursor(&LCD1, 0,0);
			for(int i=0;i<13;i++){
				CLCD_I2C_WriteChar(&LCD1, Tlane[i]);
			}
			CLCD_I2C_WriteChar(&LCD1, 0xFF);
			sprintf(lcd_text, "%2d", _level);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
			
			CLCD_I2C_SetCursor(&LCD1, 0,1);
			for(int i=0;i<13;i++){
				CLCD_I2C_WriteChar(&LCD1, Blane[i]);
			}
			CLCD_I2C_WriteChar(&LCD1, 0xFF);
			sprintf(lcd_text, "%2d", _score);
			CLCD_I2C_WriteString(&LCD1,lcd_text);
		}
	}
}	

void task_button(void * params){
	uint32_t btn; 
	for(;;){
		xQueueReceive(qbutton_hd, &btn, portMAX_DELAY);
		xSemaphoreTake(myMutex, portMAX_DELAY);
		if (btn == 0){
				if     (Tlane[0]== __u)  {Tlane[0]= u__; allLane[0][0] = 3; allLane[1][0] = 0;}
				else if(Blane[0]== u__ && Tlane[0]==0x20)
                                 {Tlane[0]= __u; Blane[0]= 0x20;
																  allLane[1][0] = 3; allLane[2][0] = 0;}
				else if(Blane[0]== __u)  {Blane[0]= u__; allLane[2][0] = 3; allLane[3][0] = 0;}
		}
		if (btn == 1){
				if     (Tlane[0]== u__)  {Tlane[0]= __u; allLane[0][0] = 0; allLane[1][0] = 3;}
				else if(Tlane[0]== __u && Blane[0]==0x20)
                                 {Tlane[0]= 0x20; Blane[0]= u__;
																  allLane[1][0] = 0; allLane[2][0] = 3;}
				else if(Blane[0]== u__)  {Blane[0]= __u; allLane[2][0] = 0; allLane[3][0] = 3;}
		}
		if (btn == 2){
				if     (Tlane[0]== u__){Tlane[1] = b__; allLane[0][1] = 1;}
				else if(Tlane[0]== __u){Tlane[1] = __b; allLane[1][1] = 1;}
				else if(Blane[0]== u__){Blane[1] = b__; allLane[2][1] = 1;}
				else if(Blane[0]== __u){Blane[1] = __b; allLane[3][1] = 1;}
		}
		xSemaphoreGive(myMutex);
		xSemaphoreGive(taskLCD_sem);
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
	BaseType_t xHPTW = pdFALSE;
	static uint32_t last_tick = 0;
	uint32_t now = xTaskGetTickCountFromISR();
	if (now - last_tick > pdMS_TO_TICKS(150)) // debounce 50ms
	{
		last_tick = now;
		uint32_t btn = 0xFF; // Invalid code
		if( GPIO_Pin == GPIO_PIN_14)    { btn = 0; }
		else if(GPIO_Pin == GPIO_PIN_10){ btn = 1; }
		else if(GPIO_Pin == GPIO_PIN_15){ btn = 2; }
		if(btn != 0xFF){
			xQueueSendFromISR(qbutton_hd, &btn, &xHPTW);
		}
		portYIELD_FROM_ISR(xHPTW);
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
