/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017-2023, Regents of the University of California.
 *
 * This file is part of NAC-ABE.
 *
 * NAC-ABE is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * NAC-ABE is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received copies of the GNU General Public License along with
 * NAC-ABE, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of NAC-ABE authors and contributors.
 */

#ifndef NAC_ABE_CRYPTO_EXECUTOR_HPP
#define NAC_ABE_CRYPTO_EXECUTOR_HPP

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <thread>

namespace ndn {
namespace nacabe {

/**
 * @brief Process-wide single-thread executor for ABE pairing operations.
 *
 * OpenABE/the underlying pairing library is not documented as safe for
 * concurrent use from multiple threads, and ABESupport keeps its own
 * process-wide caches (e.g. the decrypted-content-key cache) that are not
 * synchronized. Every Producer::produceAsync() and Consumer decrypt posts its
 * pairing work through this single shared thread, which guarantees at most
 * one ABE pairing operation runs at a time in the process, regardless of how
 * many Producer/Consumer objects exist.
 *
 * Running pairing operations here (instead of on the caller's Face
 * io_context thread) keeps Interest/Data processing on that Face responsive
 * while a pairing op is in flight.
 *
 * Lifetime note: this is a Meyers singleton, started on first use and torn
 * down at static destruction (process exit). It does not track or wait for
 * individual callers' in-flight jobs, so applications must not destroy a
 * Producer/Consumer while one of its produceAsync()/consume() calls is still
 * in flight.
 */
class CryptoExecutor
{
public:
  static CryptoExecutor&
  getInstance();

  CryptoExecutor(const CryptoExecutor&) = delete;
  CryptoExecutor&
  operator=(const CryptoExecutor&) = delete;

  boost::asio::io_context&
  ioContext()
  {
    return m_io;
  }

private:
  CryptoExecutor();
  ~CryptoExecutor();

  boost::asio::io_context m_io;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
  std::thread m_thread;
};

} // namespace nacabe
} // namespace ndn

#endif // NAC_ABE_CRYPTO_EXECUTOR_HPP
