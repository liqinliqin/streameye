
/*
 * Copyright (c) Calin Crisan
 * This file is part of streamEye.
 *
 * streamEye is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __COMMON_H
#define __COMMON_H

#define DEBUG(fmt, ...)                 if (log_level >= 2) fprintf(stderr, "%s: DEBUG: " fmt "\n", str_timestamp(), ##__VA_ARGS__)
#define INFO(fmt, ...)                  if (log_level >= 1) fprintf(stderr, "%s: INFO : " fmt "\n", str_timestamp(), ##__VA_ARGS__)
#define ERROR(fmt, ...)                 if (log_level >= 0) fprintf(stderr, "%s: ERROR: " fmt "\n", str_timestamp(), ##__VA_ARGS__)
#define ERRNO(msg)                      ERROR("%s: %s", msg, strerror(errno))
#define DEBUG_CLIENT(client, fmt, ...)  DEBUG("%s:%d: " fmt, client->addr, client->port, ##__VA_ARGS__)
#define INFO_CLIENT(client, fmt, ...)   INFO("%s:%d: " fmt, client->addr, client->port, ##__VA_ARGS__)
#define ERROR_CLIENT(client, fmt, ...)  ERROR("%s:%d: " fmt, client->addr, client->port, ##__VA_ARGS__)
#define ERRNO_CLIENT(client, msg)       ERROR_CLIENT(client, "%s: %s", msg, strerror(errno))

#define MIN(a, b)                       ((a) < (b) ? (a) : (b))

char                  * str_timestamp();


#endif /* __COMMON_H */
