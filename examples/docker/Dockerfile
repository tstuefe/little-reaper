FROM ubuntu:20.04

RUN apt-get update
RUN apt-get -qy install gcc-8

RUN mkdir /tmp/tinyreaper
WORKDIR /tmp/tinyreaper

COPY tinyreaper.c .
RUN gcc-8 tinyreaper.c -o tinyreaper
RUN chmod +x tinyreaper
RUN cp tinyreaper /opt

WORKDIR /

ENV PATH=${PATH}:/opt

