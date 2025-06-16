# Bachelor Project

## Introduction

My name is Cao Nguyễn Hoàng Hải 20213609. My project is using CNN model to test all the 10,000 validation images from CIFAR-10 dataset using STM32F4 Discovery. The results from STM32CubeIDE have been compared with the Google Colab version.

For Embedded C codes of SD Card Read/Write Module: https://blog.naver.com/PostView.naver?blogId=eziya76&logNo=221188701172

## Hardware Needed

1. STM32F4 Discovery Microcontroller
2. [SD Card Module](https://linhkienchatluong.vn/module-doc-the-nho/module-doc-the-sd-card_sp497_ct206.aspx)
3. [MicroSD Kingston Class 10 32GB](https://cellphones.com.vn/the-nho-microsd-kingston-class-10-non-adapter-32gb.html) with [Adapter](https://tuanphong.vn/adapter-the-nho/adapter-microsd-to-sd)

Note: The MicroSD must be in FAT32 format.

## Hardware Connection

This is the connection between STM32F4 Discovery and SD Card Module:

...

## Testing the First 1,000 Validation Images

**Step 1:** Run [cifar10_training_parameters_generated.ipynb](software_implementation/cifar10_training_parameters_generated.ipynb) (with Python 3 and T4 GPU) to generate [cnn_params.h](software_implementation/cnn_params.h) (trained weights for MCU) and [cifar10_cnn.weights.h5](software_implementation/cifar10_cnn.weights.h5) (trained weights for evaluation with Google Colab).

**Step 2:** Run [cifar10_validation_images_txt_generated.ipynb](software_implementation/cifar10_validation_images_txt_generated.ipynb) to generate generate 2 zip folders of [cifar10_essential](https://mega.nz/folder/dJxCEIha#ggBgeCuhP4gDa195bdPYaw/folder/QMBiQZjY) and [cifar10_full_dataset](https://mega.nz/folder/dJxCEIha#ggBgeCuhP4gDa195bdPYaw/folder/gAAg1ZjS). Put all the cifar10_batch_1.txt, cifar10_batch_2.txt, ..., cifar10_batch_10.txt from [cifar10_full_dataset](https://mega.nz/folder/dJxCEIha#ggBgeCuhP4gDa195bdPYaw/folder/gAAg1ZjS) folder into the SD Card.

**Step 3:** Configure .ioc file in STM32CubeIDE:
- RCC: HSE to "Crystal/Ceramic Resonator".
- SYS: Debug to "Serial Wire".
- SPI1: Mode to "Full-Duplex Master".
- FATFS (User-defined): USE_LFN to "Enabled with static working buffer on the BSS"; MAX_SS to "4096".
- Set pin PC4 to GPIO_Output.
- In clock configuration, configure HCLK to a maximum of 168 MHz.

**Step 4:** After automatically generating codes when Ctrl+S the .ioc file:
- Adding fatfs_sd.c and fatfs_sd.h
- Adding cnn_params.h and cnn_params.c
- In stm32f4xx_it.c, adding codes at:
  + /* USER CODE BEGIN 0 */
  + void SysTick_Handler(void)
 - Configure "return" at user_diskio.c using functions in fatfs_sd.h
 - In syscalls.c,
   + Adding these line codes https://github.com/niekiran/Embedded-C/blob/master/All_source_codes/target/itm_send_data.c
   + In _write function, change "__io_putchar(*ptr++);" to "ITM_SendChar(*ptr++);".
 - Configure the main.c
   + /* USER CODE BEGIN Includes */
   + /* USER CODE BEGIN PD */
   + /* USER CODE BEGIN PFP */
   + /* USER CODE BEGIN 0 */
   + /* USER CODE BEGIN 2 */
   + /* USER CODE BEGIN 3 */

**Step 5:** Run [evaluation.ipynb](software_implementation/evaluation.ipynb). It generates [google_colab_predictions.txt](software_implementation/google_colab_predictions.txt) for comparing with the predictions in ...
