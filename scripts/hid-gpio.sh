#! /usr/bin/env bash


gpio_chip="${1}"


function gpio_export_all() {

    if [ ! -e "/sys/class/gpio/${gpio_chip}" ]; then
        return
    fi

    ngpio="$(expr $(cat /sys/class/gpio/${gpio_chip}/ngpio) - 1)"
    base="$(cat /sys/class/gpio/${gpio_chip}/base)"

    for i in $(seq 0 ${ngpio}); do
        io_num=$(( $i + ${base} ))
        echo "exporting $io_num ..."
        echo ${io_num} > /sys/class/gpio/export
    done
}

function gpio_unexport_all() {
    if [ ! -e "/sys/class/gpio/${gpio_chip}" ]; then
        return
    fi

    ngpio="$(expr $(cat /sys/class/gpio/${gpio_chip}/ngpio) - 1)"
    base="$(cat /sys/class/gpio/${gpio_chip}/base)"
    for i in $(seq 0 ${ngpio}); do
        io_num=$(( $i + ${base} ))
        echo "unexporting $io_num ..."
        [ -e "/sys/class/gpio/gpio${io_num}" ] && echo ${io_num} > /sys/class/gpio/unexport
    done
}

gpio_unexport_all

rmmod hid-gpio

rmmod hid-composite

modprobe hid-composite

insmod /lib/modules/5.15.71-lts-next/extra/hid-gpio.ko

gpio_export_all