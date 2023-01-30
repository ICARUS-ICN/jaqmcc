/*
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Miguel Rodríguez Pérez <miguel@det.uvigo.gal>
 */

#include <ns3/core-module.h>
#include <ns3/ndnSIM-module.h>
#include <ns3/ndnSIM/NFD/daemon/face/face.hpp>
#include <ns3/ndnSIM/NFD/daemon/face/generic-link-service.hpp>
#include <ns3/network-module.h>
#include <ns3/point-to-point-module.h>

#include "consumer-src.hpp"

#include <string>

namespace ns3 {
using std::string;

namespace {
void
queueChange(Ptr<OutputStreamWrapper> stream, uint32_t oldSize, uint32_t newSize)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << oldSize << '\t' << newSize
                       << std::endl;
}

void
doubleValue(Ptr<OutputStreamWrapper> stream, const string& nodeName, double oldValue,
            double newValue)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << nodeName << '\t' << oldValue
                       << '\t' << newValue << std::endl;
}

void
receivedData(Ptr<OutputStreamWrapper> stream, uint comm, shared_ptr<const ndn::Data> data,
             Ptr<ndn::App> app, shared_ptr<ndn::Face>)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << comm << '\t'
                       << data->getContent().size() << std::endl;
}

} // namespace

auto
main(int argc, char* argv[]) -> int
{
  string congProto = "SBINOM";
  string topologyFile = "scenarios/scenario-cascade.txt";
  Time lapse = Seconds(20);
  uint16_t payloadSize = 1450;

  CommandLine cmd;
  cmd.Usage("Linear topology with a single source.\n"
            "\n");

  cmd.AddValue("topoFile", "Topology description file", topologyFile);
  cmd.AddValue("lapse", "Time between start of communications", lapse);
  cmd.AddValue("payload", "Payload size in bytes", payloadSize);
  cmd.Parse(argc, argv);

  AnnotatedTopologyReader topologyReader("", 25);
  topologyReader.SetFileName(topologyFile);
  topologyReader.Read();

  AsciiTraceHelper asciiTraceHelper;
  // Trace Src->Rtr queue length
  // FIXME: Check that we have selected the proper device
  auto qSizeStream1 = asciiTraceHelper.CreateFileStream("queue-cascade-1.dat");
  Config::ConnectWithoutContext("Names/Rtr2/DeviceList/1/TxQueue/PacketsInQueue",
                                MakeBoundCallback(&queueChange, qSizeStream1));
  auto qSizeStream2 = asciiTraceHelper.CreateFileStream("queue-cascade-2.dat");
  Config::ConnectWithoutContext("Names/Rtr3/DeviceList/2/TxQueue/PacketsInQueue",
                                MakeBoundCallback(&queueChange, qSizeStream2));
  auto qSizeStream3 = asciiTraceHelper.CreateFileStream("queue-cascade-3.dat");
  Config::ConnectWithoutContext("Names/Src1/DeviceList/0/TxQueue/PacketsInQueue",
                                MakeBoundCallback(&queueChange, qSizeStream3));

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.setPolicy("nfd::cs::lru");
  ndnHelper.setCsSize(
    10); // We do not need a big CS store for this simulation. In fact, 1 should do it.
  ndnHelper.InstallAll();

  // Choosing forwarding strategy
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerSrc");
  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetAttribute("PayloadSize", UintegerValue(payloadSize));
  auto producerNode = Names::Find<Node>("Src1");
  // Trace window size
  auto wsizetream = asciiTraceHelper.CreateFileStream("src-size-cascade.dat");
  auto wStream = asciiTraceHelper.CreateFileStream("src-w-cascade.dat");
  for (uint comm = 1; comm <= 4; comm++) {
    ostringstream consumerName;
    ostringstream producerName;
    ostringstream nodeName;

    consumerName << "Comm_" << comm;
    producerName << "/src" << comm;
    nodeName << "Dst" << comm;

    consumerHelper.SetPrefix(producerName.str());
    auto consumer = consumerHelper.Install(nodeName.str()).Get(0);
    // Source cannot start at 0.0 as nodes are not yet ready. First packet would get lost.
    if (comm == 1) {
      consumer->SetAttribute("DumpCongestion", BooleanValue(true));
    }
    consumer->SetStartTime(lapse * (comm - 1) + NanoSeconds(1));
    consumer->SetStopTime(lapse * (4 + 4 - comm));
    consumer->TraceConnectWithoutContext("WindowTrace", MakeBoundCallback(&doubleValue, wStream,
                                                                          consumerName.str()));
    consumer->TraceConnectWithoutContext("ReceivedDatas",
                                         MakeBoundCallback(&receivedData, wsizetream, comm));

    ndnGlobalRoutingHelper.AddOrigins(producerName.str(), producerNode);
    producerHelper.SetPrefix(producerName.str());
    producerHelper.Install(producerNode);
  }

  ndn::AppDelayTracer::InstallAll("app-delay-cascade.dat");

  // Calculate and install FIBs
  ndn::GlobalRoutingHelper::CalculateRoutes();

  Simulator::Stop(2 * 4 * lapse);

  Simulator::Run();

  Simulator::Destroy();

  return 0;
}
} // namespace ns3

auto
main(int argc, char** argv) -> int
{
  return ns3::main(argc, argv);
}
