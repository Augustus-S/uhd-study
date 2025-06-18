//
// Copyright 2010-2011 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#pragma once

#include <uhd/transport/bounded_buffer.ipp> //detail

namespace uhd { namespace transport {

/*!
 * Implement a templated bounded buffer:
 * Used for passing elements between threads in a producer-consumer model.
 * The bounded buffer implemented waits and timed waits with condition variables.
 * The pop operation blocks on the bounded_buffer to become non empty.
 * The push operation blocks on the bounded_buffer to become non full.
 */
/*!
 * 实现一个模板化的有界缓冲区（bounded buffer）：
 * 用于在线程之间按照生产者-消费者模型传递元素。
 * 该有界缓冲区的实现使用条件变量进行等待和超时等待。
 * pop 操作 在缓冲区非空之前会阻塞；
 * push 操作 在缓冲区未满之前会阻塞。
 */
template <typename elem_type>
class bounded_buffer
{
public:
    /*!
     * Create a new bounded buffer object.
     * \param capacity the bounded_buffer capacity
     */
    bounded_buffer(size_t capacity) : _detail(capacity)
    {
        /* NOP */
    }

    /*!
     * Push a new element into the bounded buffer immediately.
     * The element will not be pushed when the buffer is full.
     * \param elem the element reference pop to
     * \return false when the buffer is full
     */
    /*!
     * 立即向有界缓冲区中推入一个新元素。
     * 当缓冲区已满时不会推入该元素。
     * \param elem 要推入的元素引用
     * \return 如果缓冲区已满则返回 false
     */
    UHD_INLINE bool push_with_haste(const elem_type& elem)
    {
        return _detail.push_with_haste(elem);
    }

    /*!
     * Push a new element into the bounded buffer.
     * If the buffer is full prior to the push,
     * make room by popping the oldest element.
     * \param elem the new element to push
     * \return true if the element fit without popping for space
     */
    /*!
     * 向有界缓冲区中推入一个新元素。
     * 如果在推入之前缓冲区已满，
     * 则通过弹出最旧的元素来腾出空间。
     * \param elem 要推入的新元素
     * \return 如果在无需弹出旧元素的情况下成功推入，则返回 true
     */
    UHD_INLINE bool push_with_pop_on_full(const elem_type& elem)
    {
        return _detail.push_with_pop_on_full(elem);
    }

    /*!
     * Push a new element into the bounded_buffer.
     * Wait until the bounded_buffer becomes non-full.
     * \param elem the new element to push
     */
    /*!
     * 向有界缓冲区中推入一个新元素。
     * 会阻塞，直到缓冲区变为非满状态。
     * \param elem 要推入的新元素
     */
    UHD_INLINE void push_with_wait(const elem_type& elem)
    {
        return _detail.push_with_wait(elem);
    }

    /*!
     * Push a new element into the bounded_buffer.
     * Wait until the bounded_buffer becomes non-full or timeout.
     * \param elem the new element to push
     * \param timeout the timeout in seconds
     * \return false when the operation times out
     */
    /*!
     * 将一个新元素推入有界缓冲区。
     * 会阻塞等待直到缓冲区非满或超时。
     * \param elem 要推入的新元素
     * \param timeout 超时时间（单位：秒）
     * \return 如果操作超时则返回 false
     */
    UHD_INLINE bool push_with_timed_wait(const elem_type& elem, double timeout)
    {
        return _detail.push_with_timed_wait(elem, timeout);
    }

    /*!
     * Pop an element from the bounded buffer immediately.
     * The element will not be popped when the buffer is empty.
     * \param elem the element reference pop to
     * \return false when the buffer is empty
     */
    /*!
     * 立即从有界缓冲区中弹出一个元素。
     * 当缓冲区为空时不会弹出任何元素。
     * \param elem 用于接收弹出元素的引用
     * \return 当缓冲区为空时返回 false
     */
    UHD_INLINE bool pop_with_haste(elem_type& elem)
    {
        return _detail.pop_with_haste(elem);
    }

    /*!
     * Pop an element from the bounded_buffer.
     * Wait until the bounded_buffer becomes non-empty.
     * 一直等待，直到 bounded_buffer 非空。
     * \param elem the element reference pop to
     */
    UHD_INLINE void pop_with_wait(elem_type& elem)
    {
        return _detail.pop_with_wait(elem);
    }

    /*!
     * Pop an element from the bounded_buffer.
     * Wait until the bounded_buffer becomes non-empty or timeout.
     * \param elem the element reference pop to
     * \param timeout the timeout in seconds
     * \return false when the operation times out
     */
    /*!
     * 从 bounded_buffer 中弹出一个元素。
     * 阻塞等待直到缓冲区非空或超时。
     * \param elem 用于接收弹出元素的引用
     * \param timeout 超时时间（单位：秒）
     * \return 若操作超时则返回 false
     */
    UHD_INLINE bool pop_with_timed_wait(elem_type& elem, double timeout)
    {
        return _detail.pop_with_timed_wait(elem, timeout);
    }

private:
    bounded_buffer_detail<elem_type> _detail;
};

}} // namespace uhd::transport
