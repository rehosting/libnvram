#!/bin/bash


docker run --rm -v $PWD:/app -w /app  ghcr.io/rehosting/embedded-toolchains_rust:latest /app/package.sh
