FROM php:8.4-fpm-bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev gdb procps \
    && rm -rf /var/lib/apt/lists/*

# opcache (ships with the image, just enable it)
RUN docker-php-ext-enable opcache

# Build the minimal Blackfire-free observer extension:
#  - reserves one op_array run_time_cache extension handle
#  - registers one fcall observer returning {begin,end} for user functions
COPY repro_observer.c /tmp/repro_observer.c
RUN set -eux; \
    gcc -shared -fPIC $(php-config --includes) /tmp/repro_observer.c \
        -o "$(php-config --extension-dir)/repro_observer.so"

COPY 99-jit-observer.ini /usr/local/etc/php/conf.d/99-jit-observer.ini

# core dumps + worker stderr to FPM log
RUN echo "rlimit_core = unlimited" >> /usr/local/etc/php-fpm.conf \
 && sed -i 's/^;\?catch_workers_output\s*=.*/catch_workers_output = yes/' /usr/local/etc/php-fpm.d/www.conf \
 && sed -i 's/^;\?decorate_workers_output\s*=.*/decorate_workers_output = no/' /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm = static'            >> /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm.max_children = 8'    >> /usr/local/etc/php-fpm.d/www.conf \
 && echo 'pm.max_requests = 0'    >> /usr/local/etc/php-fpm.d/www.conf

WORKDIR /app
