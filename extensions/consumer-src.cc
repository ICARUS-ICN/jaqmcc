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

#include "consumer-src.hpp"
#include "ns3/nstime.h"
#include <ndn-cxx/util/random.hpp>
#include <utility>

NS_LOG_COMPONENT_DEFINE("ndn.ConsumerSrc");

namespace ns3

{
namespace ndn {
using ::ndn::random::getRandomNumberEngine;

NS_OBJECT_ENSURE_REGISTERED(ConsumerSrc);

auto
ConsumerSrc::GetTypeId() -> TypeId
{
  static TypeId tid =
    TypeId("ns3::ndn::ConsumerSrc")
      .SetGroupName("Ndn")
      .SetParent<ConsumerWindow>()
      .AddConstructor<ConsumerSrc>()
      .AddAttribute("Beta", "TCP Multiplicative Decrease factor", DoubleValue(0.5),
                    MakeDoubleAccessor(&ConsumerSrc::m_beta), MakeDoubleChecker<double>())
      .AddAttribute("AddRttSuppress",
                    "Minimum number of RTTs (1 + this factor) between window "
                    "decreases",
                    DoubleValue(0.5), // This default value was chosen after manual testing
                    MakeDoubleAccessor(&ConsumerSrc::m_addRttSuppress), MakeDoubleChecker<double>())
      .AddAttribute("DumpCongestion", "Dump congestion statistics to stdout", BooleanValue(false),
                    MakeBooleanAccessor(&ConsumerSrc::dumpCongestion), MakeBooleanChecker());

  return tid;
}

ConsumerSrc::ConsumerSrc()
  : m_ssthresh(std::numeric_limits<double>::max())
  , m_highData(0)
  , m_recPoint(0.0)
  , m_cubicWmax(0)
  , m_cubicLastWmax(0)
  , m_cubicLastDecrease(ns3::Simulator::Now())
{
}

void
ConsumerSrc::OnData(shared_ptr<const Data> data)
{
  ns3::ndn::Consumer::OnData(data);

  uint64_t sequenceNum = data->getName().get(-1).toSequenceNumber();

  // Set highest received Data to sequence number
  if (m_highData < sequenceNum) {
    m_highData = sequenceNum;
  }

  if (CongestionDetected(*data)) {
    if (dumpCongestion) {
      std::cout << ns3::Simulator::Now().GetSeconds() << " Congestion" << std::endl;
    }
    WindowDecrease();
  }
  else {
    WindowIncrease();
  }

  m_inFlight = m_seqTimeouts.size();

  NS_LOG_DEBUG("Window: " << std::dec << m_window << ", InFlight: " << m_inFlight);

  ScheduleNextPacket();
}

void
ConsumerSrc::WindowIncrease() noexcept
{
  if (m_window < m_ssthresh) {
    m_window += 1.0;
  }
  else {
    m_window += 1.0 / m_window;
  }
}

void
ConsumerSrc::WindowDecrease() noexcept
{
  // if (m_highData > m_recPoint) {
  const double diff = m_seq - m_highData;
  BOOST_ASSERT(diff > 0);

  m_recPoint = m_seq + (m_addRttSuppress * diff);

  m_ssthresh = m_window * m_beta;
  m_window = m_ssthresh;

  if (m_window < m_initialWindow) {
    m_window = m_initialWindow;
  }
}

void
ConsumerSrc::OnTimeout(uint32_t sequenceNum)
{
  WindowDecrease();

  m_inFlight = m_seqTimeouts.size();

  std::cerr << "Timeout Window: " << std::dec << m_window << ", InFlight: " << m_inFlight
            << std::endl;

  ns3::ndn::Consumer::OnTimeout(sequenceNum);
}

auto
ConsumerSrc::CongestionDetected(const Data& data) noexcept -> bool
{
  const uint64_t mark = data.getCongestionMark();
  const uint32_t routerId = (mark >> 32U);

  if ((mark & 0x800000U) == 0x800000U) { // A rate
    const uint8_t exponent = mark & 0xFFU;
    const uint64_t characteristic = (mark & 0x7FFF00U) >> 8;
    const double rate = characteristic << exponent;

    m_routerInfo[routerId].SetRate(rate);
    if (dumpCongestion) {
      std::cout << ns3::Simulator::Now().GetSeconds() << " RID: " << routerId << " rate: " << rate
                << std::endl;
    }
  }
  else {
    m_routerInfo[routerId].SetDelay(MicroSeconds(mark & 0x7FFFFFU));
    if (dumpCongestion) {
      std::cout << ns3::Simulator::Now().GetSeconds() << " RID: " << routerId
                << " delay: " << MicroSeconds(mark & 0x7FFFFFU).GetSeconds() << std::endl;
    }
  }

  RouterStatus& rInfo = m_routerInfo.at(routerId);
  const double rate = rInfo.GetRate();
  const Time delay = rInfo.GetDelay();
  if (rate == 0) {
    return false;
  }

  /* This is a security fallback: As this implementation does not retransmit
   * lost packets, when there is a severe loss the simulation waits for all
   * lost packets to timeout. This takes a lot of time and, basically means that
   * the sender restarts in congestion avoidance with a window of 1 (after n
   * timeouts). This usually happens during the slow-start phase, so, if the
   * estimated queue grows grater than, lets say, 100 packets, it is a
   * congestion event */
  if (rInfo.GetDelay().GetSeconds() * rInfo.GetRate() > 100 * m_payloadSize
      && m_window < m_ssthresh) {
    return true;
  }

  if (delay > MilliSeconds(5)) { // Codel high mark
    const Time now = ns3::Simulator::Now();

    if (rInfo.GetNextMarkTime() == Time::Max()) {
      rInfo.SetNextMarkTime(now + rInfo.GetInterval());
    }
    else if (now > rInfo.GetNextMarkTime()) {
      rInfo.IncCount();
      const Time currentInterval =
        Seconds(rInfo.GetInterval().GetSeconds() / sqrt(rInfo.GetCount() + 1));

      rInfo.SetNextMarkTime(rInfo.GetNextMarkTime() + currentInterval);

      // FIXME: Maybe return congestion mark
      const double sessRate =
        m_window.Get() * m_payloadSize / m_rtt->GetCurrentEstimate().GetSeconds();
      const double guilt = sessRate / rate;

      std::bernoulli_distribution congested(guilt);

      return congested(getRandomNumberEngine());
    }
  }
  else if (rInfo.GetNextMarkTime() != Time::Max()) {
    rInfo.SetNextMarkTime(Time::Max());
    rInfo.SetCount(0);
  }

  return false;
}

auto
ConsumerSrc::RouterStatus::SetRate(uint64_t rate) -> RouterStatus&
{
  m_currentRate = rate;

  return *this;
}

auto
ConsumerSrc::RouterStatus::SetDelay(const Time& delay) -> RouterStatus&
{
  m_currentDelay = delay;

  return *this;
}

auto
ConsumerSrc::RouterStatus::GetRate() const -> uint64_t
{
  return m_currentRate;
}

auto
ConsumerSrc::RouterStatus::GetDelay() const -> Time
{
  return m_currentDelay;
}

ConsumerSrc::RouterStatus::RouterStatus(uint64_t rate, Time delay)
  : m_currentRate(rate)
  , m_currentDelay(std::move(delay))
  , m_interval(MilliSeconds(100))
  , m_count(0)
  , m_nextMarkTime(Time::Max())
{
}

auto
ConsumerSrc::RouterStatus::GetCount() const noexcept -> uint8_t
{
  return m_count;
}

void
ConsumerSrc::RouterStatus::IncCount() noexcept
{
  m_count += 1;
}

auto
ConsumerSrc::RouterStatus::GetNextMarkTime() const noexcept -> Time
{
  return m_nextMarkTime;
}
void
ConsumerSrc::RouterStatus::SetNextMarkTime(const Time& time) noexcept
{
  m_nextMarkTime = time;
}
void
ConsumerSrc::RouterStatus::SetCount(uint8_t count) noexcept
{
  m_count = count;
}
} // namespace ndn

} // namespace ns3
