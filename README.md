# kotetsu

-- *Love in the Time of Corona*

E-ink Display driven by Raspberry Pi, powered by two 18650 rechargable batteries.

## Getting Started

### Prerequisites

Update (recommended)
```bash
sudo apt update

sudo apt upgrade
```

Enable automatic loading of SPI kernel module
```bash
sudo raspi-config

# Interfacing Options - P4 SPI - Enable

sudo reboot
```

BCM2835
```bash
wget http://www.airspayce.com/mikem/bcm2835/bcm2835-1.68.tar.gz

tar zxvf bcm2835-1.68.tar.gz

cd bcm2835-1.68/

sudo ./configure && sudo make && sudo make check && sudo make install
```

wiringPi (optional)
```bash
sudo apt-get install wiringpi

gpio -v
```

### Compiling

```bash
make

sudo ./kotetsu
```

## Demo

![IMG_5190](https://user-images.githubusercontent.com/46537987/151684689-d26080f0-e15d-479a-b8f1-a48a87c5b144.jpg)
![IMG_5189](https://user-images.githubusercontent.com/46537987/151684695-60c187ae-a488-49db-99dd-8d8c69d13cb4.jpg)
![IMG_5188](https://user-images.githubusercontent.com/46537987/151684697-881ef347-b77e-42bd-8be1-c1a8f06b7331.jpg)

## Design
![kotetsu_xml](https://user-images.githubusercontent.com/46537987/151235465-79709578-8311-4fe6-a960-31c8aee52772.png)

## License

This project is licensed under the MIT License - see the ![LICENSE.md](https://github.com/k27dong/kotetsu/blob/master/LICENSE) file for details