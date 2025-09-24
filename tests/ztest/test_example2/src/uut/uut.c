/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <uut.h>
#include <foo/foo.h>
#include <zephyr/sys/util.h>
#include <stddef.h>

int
uut_init(void* handle)
{
    int err;

    if (handle == NULL)
    {
        return -1;
    }

    err = foo_init(handle);

    if (err != 0)
    {
        return err;
    }

    return foo_execute();
}
