FROM php:8.4-fpm-bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
        $PHPIZE_DEPS gdb procps \
    && rm -rf /var/lib/apt/lists/*

# opcache (ships with the image, just enable it)
RUN docker-php-ext-enable opcache

# Build the observer extension with the standard PHP toolchain (phpize/configure/make).
COPY config.m4 repro_observer.c /usr/src/repro_observer/
RUN set -eux; \
    cd /usr/src/repro_observer; \
    phpize; \
    ./configure --enable-repro_observer; \
    make -j"$(nproc)"; \
    make install

COPY 99-jit-observer.ini /usr/local/etc/php/conf.d/99-jit-observer.ini

# core dumps + worker stderr to FPM log
RUN echo "rlimit_core = unlimited" >> /usr/local/etc/php-fpm.conf \
 && sed -i 's/^;\?catch_workers_output\s*=.*/catch_workers_output = yes/' /usr/local/etc/php-fpm.d/www.conf \
 && sed -i 's/^;\?decorate_workers_output\s*=.*/decorate_workers_output = no/' /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm = static'            >> /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm.max_children = 8'    >> /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm.max_requests = 0'    >> /usr/local/etc/php-fpm.d/www.conf

WORKDIR /app
