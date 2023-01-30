/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 * Copyright (c) 2020-2023 Universidade de Vigo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef NDN_CONSUMER_SRC_H
#define NDN_CONSUMER_SRC_H

#include <ns3/ndnSIM/model/ndn-common.hpp>

#include <ns3/ndnSIM/apps/ndn-consumer-window.hpp>

namespace ns3 {
namespace ndn {
class ConsumerSrc : public ConsumerWindow {
public:
  static auto GetTypeId() -> TypeId;

  ConsumerSrc();

  void OnData(shared_ptr<const Data> data) override;

  void OnTimeout(uint32_t sequenceNum) override;

private:
  void WindowIncrease() noexcept;
  void WindowDecrease() noexcept;
  class RouterStatus {
  public:
    explicit RouterStatus(uint64_t rate = 0, Time delay = Seconds(0));

    auto GetDelay() const -> Time;

    auto GetRate() const -> uint64_t;

    auto SetDelay(const Time& delay) -> RouterStatus&;

    auto SetRate(uint64_t rate) -> RouterStatus&;

    auto
    GetInterval() const noexcept -> Time
    {
      return m_interval;
    }

    auto GetCount() const noexcept -> uint8_t;
    void IncCount() noexcept;
    void SetCount(uint8_t count) noexcept;
    auto GetNextMarkTime() const noexcept -> Time;
    void SetNextMarkTime(const Time& delay) noexcept;

  private:
    uint64_t m_currentRate;
    Time m_currentDelay;

    // CoDel related status
    Time m_interval;
    uint8_t m_count;
    Time m_nextMarkTime;
  };

  std::map<uint32_t, RouterStatus> m_routerInfo;

  auto CongestionDetected(const Data& data) noexcept -> bool;

  TracedValue<double> m_ssthresh;
  uint32_t m_highData;
  double m_recPoint;
  double m_beta;
  double m_addRttSuppress;
  bool dumpCongestion;

  // TCP CUBIC Parameters //
  static constexpr double CUBIC_C = 0.4;
  static constexpr double m_cubicBeta = 0.7;

  double m_cubicWmax;
  double m_cubicLastWmax;
  Time m_cubicLastDecrease;
};
} // namespace ndn
} // namespace ns3

#endif // NDN_CONSUMER_SRC_H
