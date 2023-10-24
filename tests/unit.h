/* unit.c API unit tests driver
 *
 * Copyright (C) 2006-2019 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */


#ifndef CyaSSL_UNIT_H
#define CyaSSL_UNIT_H

#include <wolfssl/ssl.h>
#include <wolfssl/test.h>    /* thread and tcp stuff */

#ifdef WOLFSSL_FORCE_MALLOC_FAIL_TEST
#define XABORT()
#else
#define XABORT() abort()
#endif

#ifndef WOLFSSL_PASSTHRU_ERR
#define Fail(description, result) do {                                         \
    printf("\nERROR - %s line %d failed with:", __FILE__, __LINE__);           \
    printf("\n    expected: "); printf description;                            \
    printf("\n    result:   "); printf result; printf("\n\n");                 \
    XABORT();                                                                  \
} while(0)
#else
#define Fail(description, result) do {                               \
    printf("\nERROR - %s line %d failed with:", __FILE__, __LINE__); \
    printf("\n    expected: ");printf description;                   \
    printf("\n    result:   "); printf result; printf("\n\n");       \
} while (0)
#endif

#define Assert(test, description, result) if (!(test)) Fail(description, result)

#define AssertTrue(x)    Assert( (x), ("%s is true",     #x), (#x " => FALSE"))
#define AssertFalse(x)   Assert(!(x), ("%s is false",    #x), (#x " => TRUE"))
#define AssertNotNull(x) Assert( (x), ("%s is not null", #x), (#x " => NULL"))

#define AssertNull(x) do {                                                     \
    void* _x = (void *) (x);                                                   \
                                                                               \
    Assert(!_x, ("%s is null", #x), (#x " => %p", _x));                        \
} while(0)

#define AssertInt(x, y, op, er) do {                                           \
    int _x = (int)x;                                                                \
    int _y = (int)y;                                                                \
                                                                               \
    Assert(_x op _y, ("%s " #op " %s", #x, #y), ("%d " #er " %d", _x, _y));    \
} while(0)

#define AssertIntEQ(x, y) AssertInt(x, y, ==, !=)
#define AssertIntNE(x, y) AssertInt(x, y, !=, ==)
#define AssertIntGT(x, y) AssertInt(x, y,  >, <=)
#define AssertIntLT(x, y) AssertInt(x, y,  <, >=)
#define AssertIntGE(x, y) AssertInt(x, y, >=,  <)
#define AssertIntLE(x, y) AssertInt(x, y, <=,  >)

#define AssertStr(x, y, op, er) do {                                           \
    const char* _x = x;                                                        \
    const char* _y = y;                                                        \
    int   _z = (_x && _y) ? strcmp(_x, _y) : -1;                               \
                                                                               \
    Assert(_z op 0, ("%s " #op " %s", #x, #y),                                 \
                                            ("\"%s\" " #er " \"%s\"", _x, _y));\
} while(0)

#define AssertStrEQ(x, y) AssertStr(x, y, ==, !=)
#define AssertStrNE(x, y) AssertStr(x, y, !=, ==)
#define AssertStrGT(x, y) AssertStr(x, y,  >, <=)
#define AssertStrLT(x, y) AssertStr(x, y,  <, >=)
#define AssertStrGE(x, y) AssertStr(x, y, >=,  <)
#define AssertStrLE(x, y) AssertStr(x, y, <=,  >)

#define EXPECT_DECLS \
    int _ret = TEST_SKIPPED
#define EXPECT_RESULT() \
    _ret
#define EXPECT_SUCCESS() \
    (_ret == TEST_SUCCESS)
#define EXPECT_FAIL() \
    (_ret == TEST_FAIL)

#define ExpFail(description, result)                                     \
    do                                                                   \
    {                                                                    \
        printf("\nERROR - %s line %d failed with:", __FILE__, __LINE__); \
        fputs("\n    expected: ", stdout);                               \
        printf description;                                              \
        fputs("\n    result:   ", stdout);                               \
        printf result;                                                   \
        fputs("\n\n", stdout);                                           \
        fflush(stdout);                                                  \
        _ret = TEST_FAIL;                                                \
    } while (0)

#define Expect(test, description, result)     \
    do                                        \
    {                                         \
        if (_ret != TEST_FAIL)                \
        {                                     \
            if (!(test))                      \
                ExpFail(description, result); \
            else                              \
                _ret = TEST_SUCCESS;          \
        }                                     \
    } while (0)

#define ExpectTrue(x) Expect((x), ("%s is true", #x), (#x " => FALSE"))
#define ExpectFalse(x) Expect(!(x), ("%s is false", #x), (#x " => TRUE"))
#define ExpectNotNull(x) Expect((x), ("%s is not null", #x), (#x " => NULL"))

#define ExpectNull(x)                                           \
    do                                                          \
    {                                                           \
        if (_ret != TEST_FAIL)                                  \
        {                                                       \
            PEDANTIC_EXTENSION void *_x = (void *)(x);          \
            Expect(!_x, ("%s is null", #x), (#x " => %p", _x)); \
        }                                                       \
    } while (0)

#define ExpectInt(x, y, op, er)                                                     \
    do                                                                              \
    {                                                                               \
        if (_ret != TEST_FAIL)                                                      \
        {                                                                           \
            int _x = (int)(x);                                                      \
            int _y = (int)(y);                                                      \
            Expect(_x op _y, ("%s " #op " %s", #x, #y), ("%d " #er " %d", _x, _y)); \
        }                                                                           \
    } while (0)

#define ExpectIntEQ(x, y) ExpectInt(x, y, ==, !=)
#define ExpectIntNE(x, y) ExpectInt(x, y, !=, ==)
#define ExpectIntGT(x, y) ExpectInt(x, y, >, <=)
#define ExpectIntLT(x, y) ExpectInt(x, y, <, >=)
#define ExpectIntGE(x, y) ExpectInt(x, y, >=, <)
#define ExpectIntLE(x, y) ExpectInt(x, y, <=, >)



void ApiTest(void);
int  SuiteTest(int argc, char** argv);
int  HashTest(void);
void SrpTest(void);


#endif /* CyaSSL_UNIT_H */
