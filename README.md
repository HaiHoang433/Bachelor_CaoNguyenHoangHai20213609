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

**Step 1:** Run [cifar10_training_parameters_generated.ipynb](software_implementation/cifar10_training_parameters_generated.ipynb) to [generate cnn_params.h](software_implementation/cnn_params.h) (trained weights for MCU) and [cifar10_cnn.weights.h5](software_implementation/cifar10_cnn.weights.h5) (trained weights for evaluation with Google Colab).
