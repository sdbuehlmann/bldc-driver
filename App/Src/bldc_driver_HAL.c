/*
 * bldc_driver_HAL.c
 *
 *  Created on: Nov 29, 2017
 *      Author: simon
 */

#include "main.h"

#include "bldc_driver_HAL.h"
#include "bldc_driver_adapter.h"

// hal handles
static ADC_HandleTypeDef *pShuntA_hallB_ADC_handle;
static ADC_HandleTypeDef *pShuntB_hallA_ADC_handle;
static ADC_HandleTypeDef *pUser_ADC_handle;
static ADC_HandleTypeDef *pMainPower_ADC_handle;

static DAC_HandleTypeDef *pVirtZero_DAC_handler;

static TIM_HandleTypeDef *pA_LS_HS_PWM_handle;
static TIM_HandleTypeDef *pB_LS_PWM_handle;
static TIM_HandleTypeDef *pC_LS_HS_PWM_handle;
static TIM_HandleTypeDef *pB_HS_PWM_handle;
static TIM_HandleTypeDef *pCallback_Timer_handle;
static TIM_HandleTypeDef *pSystemtime_Timer_handle;

static SPI_HandleTypeDef *pSPI_handle;

static UART_HandleTypeDef *pUART_handle;

// comperator interrupt listeners
static void (*listenerPhaseA)(uint8_t edge);
static void (*listenerPhaseB)(uint8_t edge);
static void (*listenerPhaseC)(uint8_t edge);

// adc new measurement data available listeners
static void (*listener_shuntB_hallA_meas)(void);
static void (*listener_shuntA_hallB_meas)(void);

// adc last datas
uint32_t last_ShuntA_HallB_ADCValue = 0;
uint32_t last_ShuntB_HallA_ADCValue = 0;

volatile uint32_t last_userIn_ADCValue = 0;
volatile uint32_t last_mainPower_ADCValue = 0;
volatile uint8_t newData_userIn_ADCValue_flag = 0;
volatile uint8_t newData_mainPower_ADCValue_flag = 0;

// spi last datas
static uint16_t statusRegister1;
static uint16_t statusRegister2;

// timer callbacks
static void (*listener_callback_timer)(void);
static uint32_t delayTimeInUs = 0;
static uint32_t elapsedTimeInUs = 0;
static uint32_t deltaSystimeInUs = 0;

void initBLDCDriver(ADC_HandleTypeDef *pShuntA_hallB_ADC_handle_param,
		ADC_HandleTypeDef *pShuntB_hallA_ADC_handle_param,
		ADC_HandleTypeDef *pUser_ADC_handle_param,
		ADC_HandleTypeDef *pMainPower_ADC_handle_param,
		DAC_HandleTypeDef *pVirtZero_DAC_handler_param,
		TIM_HandleTypeDef *pA_LS_HS_PWM_handle_param,
		TIM_HandleTypeDef *pB_LS_PWM_handle_param,
		TIM_HandleTypeDef *pC_LS_HS_PWM_handle_param,
		TIM_HandleTypeDef *pB_HS_PWM_handle_param,
		TIM_HandleTypeDef *pCallback_Timer_param,
		TIM_HandleTypeDef *pSystemtime_Timer_param,
		SPI_HandleTypeDef *pSPI_handle_param,
		UART_HandleTypeDef *pUART_handle_param) {
	pShuntA_hallB_ADC_handle = pShuntA_hallB_ADC_handle_param;
	pShuntB_hallA_ADC_handle = pShuntB_hallA_ADC_handle_param;
	pUser_ADC_handle = pUser_ADC_handle_param;
	pMainPower_ADC_handle = pMainPower_ADC_handle_param;
	pVirtZero_DAC_handler = pVirtZero_DAC_handler_param;
	pA_LS_HS_PWM_handle = pA_LS_HS_PWM_handle_param;
	pB_LS_PWM_handle = pB_LS_PWM_handle_param;
	pC_LS_HS_PWM_handle = pC_LS_HS_PWM_handle_param;
	pB_HS_PWM_handle = pB_HS_PWM_handle_param;
	pCallback_Timer_handle = pCallback_Timer_param;
	pSystemtime_Timer_handle = pSystemtime_Timer_param;
	pUART_handle = pUART_handle_param;
	pSPI_handle = pSPI_handle_param;

}
void startupBLDCDriver() {
	startup();
}
void proceedBLDCDriver() {
	proceed();
}

//========================= GPIO'S ===================================
void initGPIOs() {
}

// led's
void switch_PowerLED(uint8_t state) {
	HAL_GPIO_WritePin(DO_LED_1_GPIO_Port, DO_LED_1_Pin, state);
}
void switch_StatusLED1(uint8_t state) {
	HAL_GPIO_WritePin(DO_LED_2_GPIO_Port, DO_LED_2_Pin, state);
}
void switch_StatusLED2(uint8_t state) {
	HAL_GPIO_WritePin(DO_LED_3_GPIO_Port, DO_LED_3_Pin, state);
}
void switch_StatusLED3(uint8_t state) {
	HAL_GPIO_WritePin(DO_LED_4_GPIO_Port, DO_LED_4_Pin, state);
}
void switch_StatusLED4(uint8_t state) {
	HAL_GPIO_WritePin(DO_LED_5_GPIO_Port, DO_LED_5_Pin, state);
}

// bridge driver
void switch_Enable_BridgeDriver(uint8_t state) {
	HAL_GPIO_WritePin(DO_DRIVER_EN_GPIO_Port, DO_DRIVER_EN_Pin, state);
}
void switch_DCCal_BridgeDriver(uint8_t state) {
	HAL_GPIO_WritePin(DO_DRIVER_DC_CAL_GPIO_Port, DO_DRIVER_DC_CAL_Pin, state);
}
uint8_t read_NFault_BridgeDriver() {
	return HAL_GPIO_ReadPin(DI_DRIVER_NFAULT_GPIO_Port, DI_DRIVER_NFAULT_Pin);
}
uint8_t read_NOCTW_BridgeDriver() {
	return HAL_GPIO_ReadPin(DI_DRIVER_NOCTW_GPIO_Port, DI_DRIVER_NOCTW_Pin);
}
uint8_t read_PWRGD_BridgeDriver() {
	return HAL_GPIO_ReadPin(DI_DRIVER_PWRGD_GPIO_Port, DI_DRIVER_PWRGD_Pin);
}

// main interface
void switch_MainSwitch(uint8_t state) {
	HAL_GPIO_WritePin(DO_MAIN_SWITCH_GPIO_Port, DO_MAIN_SWITCH_Pin, state);
}
uint8_t read_MainButton() {
	return HAL_GPIO_ReadPin(DI_MAIN_BUTTON_GPIO_Port, DI_MAIN_BUTTON_Pin);
}
uint8_t read_StateButton() {
	return HAL_GPIO_ReadPin(DI_USER_IN_GPIO_Port, DI_USER_IN_Pin);
}

//========================= UART ===================================
void initUART() {
	/*switch_PowerLED(1);

	 switch (HAL_UART_GetState(pUART_handle)) {
	 case HAL_UART_STATE_READY:
	 switch_StatusLED1(1);
	 break;
	 case HAL_UART_STATE_ERROR:
	 switch_StatusLED2(1);
	 break;
	 case HAL_UART_STATE_RESET:
	 switch_StatusLED3(1);
	 break;
	 default:
	 switch_StatusLED4(1);
	 break;
	 }*/

	/*uint8_t aTxBuffer[] = "Hello";
	 if(HAL_UART_Transmit(&uart_handle, (uint8_t*)aTxBuffer, 5, 5000)!= HAL_OK)
	 {
	 switch_PowerLED(0);
	 }*/
}

void transmitStringOverUART(uint8_t *pMsg) {
	// find lenght of string (zero terminated)
	uint8_t *pTemp = pMsg;
	uint8_t cnt = 0;
	while (1) {
		if (*pTemp == 0 || cnt >= 255) {
			// end of string
			break;
		} else {
			// count up pointer to next string element
			pTemp++;
			cnt++;
		}
	}

	HAL_UART_Transmit(pUART_handle, pMsg, cnt, 100);
}
void transmitCharOverUART(char data) {
	HAL_UART_Transmit(pUART_handle, &data, 1, 100);
}

//========================= COMPERATORS ==============================
void initComp() {
}

void register_comperatorListener_phaseA(void (*listener)(uint8_t)) {
	listenerPhaseA = listener;
}
void register_comperatorListener_phaseB(void (*listener)(uint8_t)) {
	listenerPhaseB = listener;
}
void register_comperatorListener_phaseC(void (*listener)(uint8_t)) {
	listenerPhaseC = listener;
}

void enableCompA(uint8_t enable) {
	if (enable) {
		HAL_NVIC_EnableIRQ(IR_COMP_A_EXTI_IRQn);
	} else {
		HAL_NVIC_DisableIRQ(IR_COMP_A_EXTI_IRQn);
	}
}
void enableCompB(uint8_t enable) {
	if (enable) {
		HAL_NVIC_EnableIRQ(IR_COMP_B_EXTI_IRQn);
	} else {
		HAL_NVIC_DisableIRQ(IR_COMP_B_EXTI_IRQn);
	}
}
void enableCompC(uint8_t enable) {
	if (enable) {
		HAL_NVIC_EnableIRQ(IR_COMP_C_EXTI_IRQn);
	} else {
		HAL_NVIC_DisableIRQ(IR_COMP_C_EXTI_IRQn);
	}
}

// interrupts
void phaseAComp_interrupt() {
	if (listenerPhaseA != 0) {
		listenerPhaseA(HAL_GPIO_ReadPin(IR_COMP_A_GPIO_Port, IR_COMP_A_Pin));
	}
}
void phaseBComp_interrupt() {
	if (listenerPhaseB != 0) {
		listenerPhaseB(HAL_GPIO_ReadPin(IR_COMP_B_GPIO_Port, IR_COMP_B_Pin));
	}
}
void phaseCComp_interrupt() {
	if (listenerPhaseC != 0) {
		listenerPhaseC(HAL_GPIO_ReadPin(IR_COMP_C_GPIO_Port, IR_COMP_C_Pin));
	}
}

//========================= ADC ==============================
void initAnalog() {
	/*switch_PowerLED(1);

	 switch (HAL_ADC_GetState(pShuntA_hallB_ADC_handle)) {
	 case HAL_ADC_STATE_READY:
	 switch_StatusLED1(1);
	 break;
	 case HAL_ADC_STATE_ERROR_INTERNAL:
	 switch_StatusLED2(1);
	 break;
	 case HAL_ADC_STATE_RESET:
	 switch_StatusLED3(1);
	 break;
	 default:
	 switch_StatusLED4(1);
	 break;
	 }*/
}

void registerListener_newMeasData_hallA_shuntB(void (*listener)(void)) {
	listener_shuntA_hallB_meas = listener;
}
void registerListener_newMeasData_hallB_shuntA(void (*listener)(void)) {
	listener_shuntB_hallA_meas = listener;
}

// shunt
int8_t start_phaseACurrentMeas_shunt() {
	return 0;
}
int8_t start_phaseBCurrentMeas_shunt() {
	return 0;
}

uint32_t getLastMeas_phaseACurrentMeas_shunt() {
	return 0;
}
uint32_t getLastMeas_phaseBCurrentMeas_shunt() {
	return 0;
}

// hall
int8_t start_phaseACurrentMeas_hall() {
	return 0;
}
int8_t start_phaseBCurrentMeas_hall() {
	return 0;
}

uint32_t getLastMeas_phaseACurrentMeas_hall() {
	return 0;
}
uint32_t getLastMeas_phaseBCurrentMeas_hall() {
	return 0;
}

// user voltage in
int8_t start_userVolatgeMeas() {
	if (isMeasReady_userVolatgeMeas()) {
		HAL_ADC_Start_IT(pUser_ADC_handle);
		return 0;
	}
	return -1;
}
uint32_t getLastMeas_userVolatgeMeas() {
	newData_userIn_ADCValue_flag = 0;
	return HAL_ADC_GetValue(pUser_ADC_handle);
}
uint8_t isMeasReady_userVolatgeMeas() {
	return (HAL_ADC_GetState(pUser_ADC_handle) && HAL_ADC_STATE_READY);
}
uint8_t newDataAvailable_userVolatgeMeas(){
	return newData_userIn_ADCValue_flag;
}

// main voltage
int8_t start_mainVoltageMeas() {
	if (isMeasReady_mainVolatgeMeas()) {
		HAL_ADC_Start_IT(pMainPower_ADC_handle);
		return 0;
	}
	return -1;
}
uint8_t isMeasReady_mainVolatgeMeas() {
	return (HAL_ADC_GetState(pMainPower_ADC_handle) && HAL_ADC_STATE_READY);
}
uint8_t newDataAvailable_mainVolatgeMeas() {
	return newData_mainPower_ADCValue_flag;
}
uint32_t getLastData_mainVoltageMeas() {
	newData_mainPower_ADCValue_flag = 0;
	return last_mainPower_ADCValue;
}

// interrupts
void hallB_shuntA_adc_interrupt() {
	last_ShuntA_HallB_ADCValue = HAL_ADC_GetValue(pShuntA_hallB_ADC_handle);

	if (listener_shuntA_hallB_meas != 0) {
		listener_shuntA_hallB_meas();
	}
}
void hallA_shuntB_adc_interrupt() {
	last_ShuntB_HallA_ADCValue = HAL_ADC_GetValue(pShuntB_hallA_ADC_handle);

	if (listener_shuntB_hallA_meas != 0) {
		listener_shuntB_hallA_meas();
	}
}

void callback_ADC_mainPower_IRQ() {
	last_mainPower_ADCValue = (HAL_ADC_GetValue(pMainPower_ADC_handle)-940)*1.35;
	newData_mainPower_ADCValue_flag = 1;
}
void callback_ADC_userIn_IRQ() {
	last_userIn_ADCValue = HAL_ADC_GetValue(pUser_ADC_handle);
	newData_userIn_ADCValue_flag = 1;
}

//========================= DAC ======================================
void enable_virtualGND(uint8_t enable) {
	if (enable) {
		HAL_DAC_Start(pVirtZero_DAC_handler, DAC_virtual_GND_channel);
	} else {
		HAL_DAC_Stop(pVirtZero_DAC_handler, DAC_virtual_GND_channel);
	}

}
void set_virtualGNDValue(uint32_t value) {
	HAL_DAC_SetValue(pVirtZero_DAC_handler, DAC_virtual_GND_channel,
			DAC_ALIGN_12B_R, value);
}

//========================= PWM ===================================
//sudo picocom --baud 9600  --parity n --databits 8 --flow n -l /dev/ttyUSB0
void initPWM() {
	HAL_TIM_StateTypeDef temp = HAL_TIM_PWM_GetState(pC_LS_HS_PWM_handle);
	switch (temp) {
	case HAL_TIM_STATE_RESET:
		transmitStringOverUART(
				"TIM in reset state (not yet initialized or disabled)");
		break;
	case HAL_TIM_STATE_ERROR:
		transmitStringOverUART("TIM in error state");
		break;
	case HAL_TIM_STATE_READY:
		transmitStringOverUART(
				"TIM in ready state (initialized and ready for use)");
		break;
	case HAL_TIM_STATE_BUSY:
		transmitStringOverUART(
				"TIM in busy state (an internal process is ongoing)");
		break;
	case HAL_TIM_STATE_TIMEOUT:
		transmitStringOverUART("TIM in timeout state");
		break;
	}

}

void set_PWM_DutyCycle(uint16_t dutyCycle) {
	__HAL_TIM_SET_COMPARE(pA_LS_HS_PWM_handle, PWM_A_HS_channel, dutyCycle);
	__HAL_TIM_SET_COMPARE(pA_LS_HS_PWM_handle, PWM_A_LS_channel, dutyCycle);
	__HAL_TIM_SET_COMPARE(pB_HS_PWM_handle, PWM_B_HS_channel, dutyCycle);
	__HAL_TIM_SET_COMPARE(pB_LS_PWM_handle, PWM_B_LS_channel, dutyCycle);
	__HAL_TIM_SET_COMPARE(pC_LS_HS_PWM_handle, PWM_C_HS_channel, dutyCycle);
	__HAL_TIM_SET_COMPARE(pC_LS_HS_PWM_handle, PWM_C_LS_channel, dutyCycle);
}

void enable_PWM_phaseA_HS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pA_LS_HS_PWM_handle, PWM_A_HS_channel);
	} else {
		HAL_TIM_PWM_Stop(pA_LS_HS_PWM_handle, PWM_A_HS_channel);
	}
}
void enable_PWM_phaseB_HS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pB_HS_PWM_handle, PWM_B_HS_channel);
	} else {
		HAL_TIM_PWM_Stop(pB_HS_PWM_handle, PWM_B_HS_channel);
	}
}
void enable_PWM_phaseC_HS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pC_LS_HS_PWM_handle, PWM_C_HS_channel);
	} else {
		HAL_TIM_PWM_Stop(pC_LS_HS_PWM_handle, PWM_C_HS_channel);
	}
}
void enable_PWM_phaseA_LS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pA_LS_HS_PWM_handle, PWM_A_LS_channel);
	} else {
		HAL_TIM_PWM_Stop(pA_LS_HS_PWM_handle, PWM_A_LS_channel);
	}
}
void enable_PWM_phaseB_LS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pB_LS_PWM_handle, PWM_B_LS_channel);
	} else {
		HAL_TIM_PWM_Stop(pB_LS_PWM_handle, PWM_B_LS_channel);
	}
}
void enable_PWM_phaseC_LS(uint8_t enable) {
	if (enable) {
		HAL_TIM_PWM_Start(pC_LS_HS_PWM_handle, PWM_C_LS_channel);
	} else {
		HAL_TIM_PWM_Stop(pC_LS_HS_PWM_handle, PWM_C_LS_channel);
	}
}

//========================= TIME ==================================
void initSystime() {
	HAL_TIM_Base_Start(pSystemtime_Timer_handle);
}
uint32_t getSystimeUs() {
	return __HAL_TIM_GET_COUNTER(pSystemtime_Timer_handle);
}
/** Calls the handed function after the handed time
 Parameter:
 time_ us    = time in us. max value: 2¹⁶ * 4 = 262'144us.
 listener    = callback. Called after the handed time.
 */
void startAfterUs(uint32_t time_us, void (*listener)(void)) {
	/*switch (HAL_TIM_Base_GetState(pCallback_Timer_handle)) {
	 case HAL_TIM_STATE_RESET:
	 transmitStringOverUART(
	 "TIM in reset state (not yet initialized or disabled)");
	 break;
	 case HAL_TIM_STATE_ERROR:
	 transmitStringOverUART("TIM in error state");
	 break;
	 case HAL_TIM_STATE_READY:
	 transmitStringOverUART(
	 "TIM in ready state (initialized and ready for use)");
	 break;
	 case HAL_TIM_STATE_BUSY:
	 transmitStringOverUART(
	 "TIM in busy state (an internal process is ongoing)");
	 break;
	 case HAL_TIM_STATE_TIMEOUT:
	 transmitStringOverUART("TIM in timeout state");
	 break;
	 }*/

	if (listener_callback_timer == 0) {
		listener_callback_timer = listener;
		delayTimeInUs = time_us;
		deltaSystimeInUs = getSystimeUs(); // TEMP!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

		if (delayTimeInUs > 0xFFFF) {
			// bigger than the 16bit timer max.
			elapsedTimeInUs = 0xFFFF;
			__HAL_TIM_SET_AUTORELOAD(pCallback_Timer_handle, 0xFFFF);
		} else {
			elapsedTimeInUs = delayTimeInUs;
			__HAL_TIM_SET_AUTORELOAD(pCallback_Timer_handle, delayTimeInUs);
		}
		__HAL_TIM_SET_COUNTER(pCallback_Timer_handle, 1); // reset timer
		__HAL_TIM_CLEAR_FLAG(pCallback_Timer_handle, TIM_FLAG_UPDATE); // reset interruptflag
		HAL_TIM_Base_Start_IT(pCallback_Timer_handle);
	}

	/*if (listener_callback_timer == 0
	 && HAL_TIM_Base_GetState(pCallback_Timer_handle) == HAL_TIM_STATE_READY) {
	 __HAL_TIM_SET_AUTORELOAD(pCallback_Timer_handle, time_us);
	 HAL_TIM_Base_Start_IT(pCallback_Timer_handle);

	 listener_callback_timer = listener;
	 transmitStringOverUART("TIM started!");
	 }*/

	/*switch (HAL_TIM_Base_GetState(pCallback_Timer_handle)) {
	 case HAL_TIM_STATE_RESET:
	 transmitStringOverUART(
	 "TIM in reset state (not yet initialized or disabled)");
	 break;
	 case HAL_TIM_STATE_ERROR:
	 transmitStringOverUART("TIM in error state");
	 break;
	 case HAL_TIM_STATE_READY:
	 transmitStringOverUART(
	 "TIM in ready state (initialized and ready for use)");
	 break;
	 case HAL_TIM_STATE_BUSY:
	 transmitStringOverUART(
	 "TIM in busy state (an internal process is ongoing)");
	 break;
	 case HAL_TIM_STATE_TIMEOUT:
	 transmitStringOverUART("TIM in timeout state");
	 break;
	 }*/
}
uint32_t getElapsedTimeInUs() {
	return __HAL_TIM_GET_COUNTER(pCallback_Timer_handle);
}

void callbackTimer_interrupt() {
	if (listener_callback_timer != 0) {
		// registered listener = valid interrupt
		HAL_TIM_Base_Stop_IT(pCallback_Timer_handle);
		__HAL_TIM_SET_COUNTER(pCallback_Timer_handle, 1); // reset timer

		deltaSystimeInUs = getSystimeUs() - deltaSystimeInUs; // TEMP!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

		if (elapsedTimeInUs >= delayTimeInUs) {
			// enough time elapsed, call the callback
			void (*temp)(void);
			temp = listener_callback_timer;
			listener_callback_timer = 0;

			temp();
		} else {
			// NOT enough time colapsed, update timer
			uint32_t deltaTimeInUs = elapsedTimeInUs - delayTimeInUs;
			if (deltaTimeInUs > 0xFFFF) {
				// bigger than the 16bit timer max.
				elapsedTimeInUs += 0xFFFF;
				__HAL_TIM_SET_AUTORELOAD(pCallback_Timer_handle, 0xFFFF);
			} else {
				elapsedTimeInUs += deltaTimeInUs;
				__HAL_TIM_SET_AUTORELOAD(pCallback_Timer_handle, deltaTimeInUs);
			}

			HAL_TIM_Base_Start_IT(pCallback_Timer_handle);
		}
	}
}

//========================= SPI ==================================
void selectBridgeDriverAsSPISlave(uint8_t enable) {
	HAL_GPIO_WritePin(DO_SELECT_BRIDGE_DRIVER_GPIO_Port,
	DO_SELECT_BRIDGE_DRIVER_Pin, !enable);
}

void initSPI() {
	selectBridgeDriverAsSPISlave(0);
}
void spi_readStatusRegisters_BLOCKING() {
	// write read status register 1 command
	// highbyte
	uint8_t send_data[2];
	uint8_t receive_data[2];

	// first cycle
	selectBridgeDriverAsSPISlave(1);
	HAL_Delay(100);

	send_data[0] = READ_STATUS_REGISTER_1_COMMAND_high;
	send_data[1] = 0;

	receive_data[0] = 0;
	receive_data[1] = 0;

	HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(pSPI_handle, send_data,
			receive_data, 2, 1000);

	switch (status) {
	case HAL_OK:
		logDEBUG("spi status: ok");
		break;
	case HAL_ERROR:
		logDEBUG("spi status: error");
		break;
	case HAL_BUSY:
		logDEBUG("spi status: busy");
		break;
	case HAL_TIMEOUT:
		logDEBUG("spi status: timeout");
		break;
	}
	selectBridgeDriverAsSPISlave(0);
	HAL_Delay(100);

	// second cycle
	selectBridgeDriverAsSPISlave(1);
	HAL_Delay(100);

	send_data[0] = READ_STATUS_REGISTER_2_COMMAND_high;
	send_data[1] = 0;

	receive_data[0] = 0;
	receive_data[1] = 0;

	status = HAL_SPI_TransmitReceive(pSPI_handle, send_data, receive_data, 2,
			1000);

	switch (status) {
	case HAL_OK:
		logDEBUG("spi status: ok");
		break;
	case HAL_ERROR:
		logDEBUG("spi status: error");
		break;
	case HAL_BUSY:
		logDEBUG("spi status: busy");
		break;
	case HAL_TIMEOUT:
		logDEBUG("spi status: timeout");
		break;
	}

	statusRegister1 = receive_data[0] << 8 + receive_data[1];

	selectBridgeDriverAsSPISlave(0);
	HAL_Delay(100);

	// thirt cycle (receive datas from previous cycle)
	selectBridgeDriverAsSPISlave(1);
	HAL_Delay(100);

	send_data[0] = 0;
	send_data[1] = 0;

	receive_data[0] = 0;
	receive_data[1] = 0;

	status = HAL_SPI_TransmitReceive(pSPI_handle, send_data, receive_data, 2,
			1000);

	switch (status) {
	case HAL_OK:
		logDEBUG("spi status: ok");
		break;
	case HAL_ERROR:
		logDEBUG("spi status: error");
		break;
	case HAL_BUSY:
		logDEBUG("spi status: busy");
		break;
	case HAL_TIMEOUT:
		logDEBUG("spi status: timeout");
		break;
	}

	statusRegister2 = receive_data[0] << 8 + receive_data[1];

	selectBridgeDriverAsSPISlave(0);
}

uint16_t getLastStatusRegister1Value() {
	return statusRegister1;
}
uint16_t getLastStatusRegister2Value() {
	return statusRegister2;
}
