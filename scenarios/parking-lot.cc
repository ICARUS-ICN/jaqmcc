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

#include <sstream>
#include <string>

namespace ns3 {
using std::string;

namespace {
void
queueChange(Ptr<OutputStreamWrapper> stream, const string& nodeName, uint32_t oldSize,
            uint32_t newSize)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << nodeName << '\t' << oldSize
                       << '\t' << newSize << std::endl;
}

void
rxTraffic(Ptr<OutputStreamWrapper> stream, const string& nodeName, Ptr<const Packet> packet)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << nodeName << '\t'
                       << packet->GetSize() + 20 /* ip header */ + 16 /* Eth header */
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
receivedData(Ptr<OutputStreamWrapper> stream, shared_ptr<const ndn::Data> data, Ptr<ndn::App> app,
             shared_ptr<ndn::Face>)
{
  *stream->GetStream() << Simulator::Now().GetSeconds() << '\t' << app->GetId() << '\t'
                       << data->getContent().size() << std::endl;
}

} // namespace

auto
main(int argc, char* argv[]) -> int
{
  string topologyFile = "scenarios/scenario-parking-lot.txt";
  uint nComms = 16;
  Time lapse = Seconds(20);

  CommandLine cmd;
  cmd.Usage("Linear topology with a n source.\n"
            "\n");

  cmd.AddValue("topoFile", "Topology description file", topologyFile);
  cmd.AddValue("nComms", "Number of simultaneous communications", nComms);
  cmd.AddValue("lapse", "Time between start of communications", lapse);
  cmd.Parse(argc, argv);

  AnnotatedTopologyReader topologyReader("", 25);
  topologyReader.SetFileName(topologyFile);
  topologyReader.Read();

  AsciiTraceHelper asciiTraceHelper;
  // Trace Src->Rtr queue lengths
  auto qSizeStream = asciiTraceHelper.CreateFileStream("queue.dat");
  for (uint router = 1; router < 16; router++) {
    ostringstream routerName;
    ostringstream queuePath;

    routerName << "R" << router;
    queuePath << "Names/" << routerName.str() << "/DeviceList/0/TxQueue/PacketsInQueue";
    Config::ConnectWithoutContext(queuePath.str(),
                                  MakeBoundCallback(&queueChange, qSizeStream, routerName.str()));
  }

  // Trace arriving data
  auto dataStream = asciiTraceHelper.CreateFileStream("recv_data.dat");
  for (uint comm = 1; comm <= 16; comm++) {
    ostringstream consumerMacRx;
    ostringstream consumerName;

    consumerName << 'C' << comm;
    consumerMacRx << "Names/" << consumerName.str() << "/DeviceList/0/MacRx";
    Config::ConnectWithoutContext(consumerMacRx.str(),
                                  MakeBoundCallback(&rxTraffic, dataStream, consumerName.str()));
  }

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.setPolicy("nfd::cs::lru");
  ndnHelper.setCsSize(10); // We do not need a big CS store for this simulation.
                           // In fact, 1 should do it.
  ndnHelper.InstallAll();

  // Set the correct CoDEL threshold value for the queue to be 5ms
  // Simulator::Schedule(Seconds(0), changeQueueTarget, "Rtr1", 50000000);

  // Choosing forwarding strategy
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerSrc");
  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
  // Trace window size
  auto wsizetream = asciiTraceHelper.CreateFileStream("src-size.dat");
  auto wStream = asciiTraceHelper.CreateFileStream("src-w.dat");
  for (uint comm = 0; comm < nComms; comm++) {
    ostringstream consumerName;
    ostringstream producerName;

    producerName << "P" << comm + 1;
    auto producerNode = Names::Find<Node>(producerName.str());

    consumerName << "C" << comm + 1;

    consumerHelper.SetPrefix(producerName.str());
    auto consumer = consumerHelper.Install(consumerName.str()).Get(0);
    // Source cannot start at 0.0 as nodes are not yet ready. First packet would
    // get lost.
    consumer->SetStartTime(lapse * comm + NanoSeconds(1));
    consumer->SetStopTime(lapse * (nComms + comm + 1));

    consumer->TraceConnectWithoutContext("WindowTrace", MakeBoundCallback(&doubleValue, wStream,
                                                                          consumerName.str()));
    consumer->TraceConnectWithoutContext("ReceivedDatas",
                                         MakeBoundCallback(&receivedData, wsizetream));

    ndnGlobalRoutingHelper.AddOrigins(producerName.str(), producerNode);
    producerHelper.SetPrefix(producerName.str());
    producerHelper.Install(producerNode);
  }

  ndn::AppDelayTracer::InstallAll("app-delay.dat");

  // Calculate and install FIBs
  ndn::GlobalRoutingHelper::CalculateRoutes();

  cerr << "Stop time: " << (2 * lapse * nComms).GetSeconds() << 's' << endl;
  Simulator::Stop(2 * lapse * nComms);

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
