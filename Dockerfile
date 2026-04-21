FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc \
        make \
        pkg-config \
        libpng-dev \
        libconfig-dev \
        python3 \
        python3-pip \
        net-tools \
        iproute2 \
        procps \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

RUN make clean && make

RUN chmod +x scripts/*.sh || true

EXPOSE 9090 9091

CMD ["./server"]
