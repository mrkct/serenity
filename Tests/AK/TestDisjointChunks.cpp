/*
 * Copyright (c) 2021, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <AK/DisjointChunks.h>
#include <AK/String.h>

TEST_CASE(basic)
{
    DisjointChunks<size_t> chunks;
    chunks.append({});
    chunks.last_chunk().append(0);
    chunks.append({});
    chunks.last_chunk().append(1);
    chunks.last_chunk().append(2);
    chunks.last_chunk().append(3);
    chunks.append({});
    chunks.append({});
    chunks.last_chunk().append(4);

    for (size_t i = 0; i < 5u; ++i)
        EXPECT_EQ(chunks.at(i), i);

    auto it = chunks.begin();
    for (size_t i = 0; i < 5u; ++i, ++it)
        EXPECT_EQ(*it, i);

    EXPECT_EQ(it, chunks.end());

    DisjointChunks<size_t> new_chunks;
    new_chunks.extend(move(chunks));
    EXPECT_EQ(new_chunks.size(), 5u);

    new_chunks.last_chunk().append(5);

    auto cut_off_slice = new_chunks.release_slice(2, 3);
    EXPECT_EQ(new_chunks.size(), 3u);
    EXPECT_EQ(cut_off_slice.size(), 3u);

    EXPECT_EQ(cut_off_slice[0], 2u);
    EXPECT_EQ(cut_off_slice[1], 3u);
    EXPECT_EQ(cut_off_slice[2], 4u);

    EXPECT_EQ(new_chunks[0], 0u);
    EXPECT_EQ(new_chunks[1], 1u);
    EXPECT_EQ(new_chunks[2], 5u);
}

TEST_CASE(spans)
{
    DisjointChunks<size_t> chunks;
    chunks.append({ 0, 1, 2, 3, 4, 5 });
    chunks.append({ 6, 7, 8, 9 });

    auto spans = chunks.spans();
    EXPECT_EQ(spans.size(), 10u);

    auto slice = spans.slice(1, 4);
    EXPECT_EQ(slice.size(), 4u);
    EXPECT_EQ(slice[0], 1u);
    EXPECT_EQ(slice[1], 2u);
    EXPECT_EQ(slice[2], 3u);
    EXPECT_EQ(slice[3], 4u);

    auto cross_chunk_slice = spans.slice(4, 4);
    EXPECT_EQ(cross_chunk_slice.size(), 4u);
    EXPECT_EQ(cross_chunk_slice[0], 4u);
    EXPECT_EQ(cross_chunk_slice[1], 5u);
    EXPECT_EQ(cross_chunk_slice[2], 6u);
    EXPECT_EQ(cross_chunk_slice[3], 7u);

    auto it = cross_chunk_slice.begin();
    for (size_t i = 0; i < 4u; ++i, ++it)
        EXPECT_EQ(*it, i + 4u);

    EXPECT_EQ(it, cross_chunk_slice.end());
}
