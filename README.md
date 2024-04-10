# Project Name

Particle simulation supported by threading and SFML library.

## Table of Contents

- [Introduction](#introduction)
- [Installation](#installation)

## Introduction

This application simulates bouncing particles. There are 3 different ways to spawn the particle. First is specifying the starting line where particles spawn. Second is specifying 2 angles where particle are arranged. Lastly each particles will have different velocity. In addition to controlling the parameters or constants, the application allows you to switch between explorer mode and developer mode. Developer mode is responsible for controlling the behavior of particle spawn. While explorer mode displays the periphery of the sprite, which is distinguished as red circular ball. During the explorer mode, the user can dictate the movement of sprite using keys w (up), a (left), s (down), d (right). To switch between two modes, tick the checkbox as indicated in the images below.

### Explorer Mode

<img src="images/demo4.png" alt="Explorer Mode" width="550" height="350">
_Explorer Mode_

### Developer Mode

<img src="images/demo5.png" alt="Developer Mode" width="550" height="350">
_Developer Mode_

### Feature 1: Equidistant Line Segments

<img src="images/demo1.png" alt="Feature 1: Equidistant Line Segments" width="550" height="350">
_1st batch_

### Feature 2: Equidistant Angle

<img src="images/demo2.png" alt="Feature 2: Equidistant Angle" width="550" height="350">
_2nd batch_

### Feature 3: Equidifferent Speed

<img src="images/demo3.png" alt="Feature 3: Equidifferent Speed" width="550" height="350">
_3rd batch_

## Installation

The required IDE to run this application is VS2022. Please be reminded that this application is not compatible with the VS2019.

To compile the cpp file clone or download this repository. Open the solution folder inside `Problem-Set3` and then follow the guide below to update the directories inside the project property. All external libraries are contained in this repo.

1. **Project directory**

<img src="images/2.png" alt="Project directory" width="600">
_Your current directory should look like this_

2. **General Configuration**

<img src="images/3.png" alt="General Configuration" width="600">
_The Visual Studio must be version 2022 running on a C++20 compiler_

![Opening Project Properties](images/2.5.png)
_Click the setting icon as indicated in the image. For `Problem-Set3` folder there are 3 solution files expected, namely Client1, Client2, and Server. Click any of the item to start the configuration_

3. **VC++ Directories**

<img src="images/4.png" alt="VC++ Directories" width="600">
_Check the Include and Library directories and change them according to your machine's specifications_

4. **Configure C++**
   v<img src="images/5.png" alt="Configure C++" width="600">

5. **Configure Linker/General**

<img src="images/6.png" alt="Configure Linker/General" width="600">

6. **Configure Linker/Input**

<img src="images/7.png" alt="Configure Linker/Input" width="600">

7. **Repeat step 2-6 in other solution folders**
   <img src="images/7.5.png" alt="Repeat Steps" width="600">

8. **Copy the required DLL files**

<img src="images/8.png" alt="Copy the required DLL files" width="600">
_You need `sfml-graphics-2.dll`, `sfml-system-2.dll`, `sfml-network-2.dll`, and `sfml-window-2.dll` pasted in the directory where the executable file is located_

9. **Paste the files to the application folder**

<img src="images/9.png" alt="Paste the files to the application folder" width="600">
_Copy paste the above dll files to following directories

    ```bash
    C:\your_address\ParticleSimulator-CPP\Problem-Set3\Problem-Set3\x64\Debug

    C:\your_address\ParticleSimulator-CPP\Problem-Set3\Problem-Set3\Client1\x64\Debug

    C:\your_address\ParticleSimulator-CPP\Problem-Set3\Problem-Set3\Client2\x64\Debug
    ```

Now the IDE can compile and run the particle simulator application\_
