FROM ubuntu:16.04

RUN apt-get update && \
    apt-get -y install \
        gcc \
        libc6-dev \
        make \
        gdb && \
    rm -rf /var/lib/apt/lists/*
