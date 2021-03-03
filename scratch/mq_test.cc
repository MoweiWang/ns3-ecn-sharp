#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/link-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/queue-disc.h"

extern "C"
{
#include "cdf.h"
}

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MQ_4");

enum AQM {
    TCN,
    ECNSharp
};

uint32_t dwrr_quantum[4]={1500,50,50,50};

// Port from Traffic Generator // Acknowledged to https://github.com/HKUST-SING/TrafficGenerator/blob/master/src/common/common.c
double
poission_gen_interval(double avg_rate) {
    if (avg_rate > 0)
        return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
        return 0;
}

uint32_t sendSize[3];
void CheckThroughput (Ptr<PacketSink> sink, uint32_t senderID, uint32_t pos) {
    uint32_t totalRecvBytes = sink->GetTotalRx ();
    uint32_t currentPeriodRecvBytes = totalRecvBytes - sendSize[senderID];
    sendSize[senderID] = totalRecvBytes;
    Simulator::Schedule (Seconds (0.02), &CheckThroughput, sink, senderID,pos);
    NS_LOG_UNCOND ("port: "<<pos<<"Flow: " << senderID << ", throughput (Gbps): " << currentPeriodRecvBytes * 8 / 0.02 / 1000000000);
}

template<typename T> T
rand_range (T min, T max)
{
    return min + ((double)max - min) * rand () / RAND_MAX;
}

std::string
GetFormatedStr (std::string id, std::string str, std::string terminal, AQM aqm)
{
    std::stringstream ss;
    if (aqm == TCN)
    {
        ss << "MQ_" << id << "_" << str << "." << terminal;
    }
    else if (aqm == ECNSharp)
    {
        ss << "MQ_ECNSharp_" << id << "_" << str << "." << terminal;
    }
    return ss.str ();
}



uint32_t nPorts = 1;
uint32_t nQueue = 3;

// buffer variables
uint32_t segmentSize = 1448;
uint32_t TotalBufferSize = 1048576; // 1MB
// std::vector<float> alpha = {1024, 2, 1.0, 1.0 / 2};

// statistical variables
std::vector<uint32_t> PortPass(nPorts, 0);
std::vector<uint32_t> PortLoss(nPorts, 0);
std::vector<uint32_t> BytesInPort(nPorts, 0);
std::vector<std::vector<uint32_t> > QueuePass(nPorts, std::vector<uint32_t>(nQueue, 0));
std::vector<std::vector<uint32_t> > QueueLoss(nPorts, std::vector<uint32_t>(nQueue, 0));
std::vector<std::vector<uint32_t> > BytesinQueue(nPorts, std::vector<uint32_t>(nQueue, 0));

void
TcBytesInQueueTrace(std::vector<Ptr<DWRRQueueDisc> > qdiscs, uint32_t pos,uint32_t queue, uint32_t oldValue, uint32_t newValue)
{
    Ptr<DWRRQueueDisc> q = qdiscs[pos];
 
    for (uint32_t i = 0; i < nQueue; i++)
    {
        
        uint32_t getNBytes = q->GetDWRRQueueDisc(i)->GetNBytes(); // Number of bytes currently stored in the queue disc
        if (getNBytes > BytesinQueue[pos][i])
        {
        QueuePass[pos][i] += getNBytes - BytesinQueue[pos][i];
        PortPass[pos] += getNBytes - BytesinQueue[pos][i];
        }
        BytesinQueue[pos][i] = getNBytes;
        // NS_LOG_UNCOND("port:"<<pos<<"queue:"<<i<<"has "<<getNBytes<<"inQueue");
    }

    uint32_t sumBytes = 0;
    std::vector<uint32_t> portSum(nPorts, 0);
    for (uint32_t i = 0; i < nPorts; i++)
    {
        q = qdiscs[i];
        for (uint32_t j = 0; j < nQueue; j++)
        {
            uint32_t getNBytes = q->GetDWRRQueueDisc(j)->GetNBytes();
            sumBytes += getNBytes;
            portSum[i] += getNBytes;
        }
    }
    q->SetTotalBufferUse(sumBytes);

    for (uint32_t i = 0; i < nPorts; i++)
    {
        q = qdiscs[i];
        for (uint32_t j = 0; j < nQueue; j++)
        {
            uint32_t threshold = TotalBufferSize;
            q->GetDWRRQueueDisc(j)->SetThreshold(threshold);
        }
    }
}

void
Drop(std::vector<Ptr<DWRRQueueDisc> > qdiscs, uint32_t pos,uint32_t queue, uint32_t oldValue, uint32_t newValue)
{
    // int32_t tag = 0;
    NS_LOG_UNCOND("port:"<<pos<<"queue:"<<queue);
    // QueueLoss[pos][tag] += p->GetPacket()->GetSize();
    // PortLoss[pos] += p->GetPacket()->GetSize();
}

int main (int argc, char *argv[])
{
    // LogComponentEnable("QueueDisc", LOG_LEVEL_ALL);
    // LogComponentEnable("DWRRQueueDisc", LOG_LEVEL_ALL);

    uint32_t numOfSenders = 5;

    std::string id = "undefined";

    std::string transportProt = "DcTcp";
    std::string aqmStr = "TCN";

    AQM aqm;
    double endTime = 0.5;

    unsigned randomSeed = 0;


    uint32_t TCNThreshold = 150;

    uint32_t ECNSharpInterval = 200;
    uint32_t ECNSharpTarget = 10;
    uint32_t ECNSharpMarkingThreshold = 150;

    CommandLine cmd;
    cmd.AddValue ("id", "The running ID", id);
    cmd.AddValue ("transportProt", "Transport protocol to use: Tcp, DcTcp", transportProt);
    cmd.AddValue ("AQM", "AQM to use: TCN and ECNSharp", aqmStr);

    cmd.AddValue ("endTime", "Simulation end time", endTime);
    cmd.AddValue ("randomSeed", "Random seed, 0 for random generated", randomSeed);

    cmd.AddValue ("TotalBufferSize", "The buffer size", TotalBufferSize);

    cmd.AddValue ("TCNThreshold", "The threshold for TCN", TCNThreshold);

    cmd.AddValue ("ECNSharpInterval", "The persistent interval for ECNSharp", ECNSharpInterval);
    cmd.AddValue ("ECNSharpTarget", "The persistent target for ECNSharp", ECNSharpTarget);
    cmd.AddValue ("ECNSharpMarkingThreshold", "The instantaneous marking threshold for ECNSharp", ECNSharpMarkingThreshold);

    cmd.Parse (argc, argv);

    if (transportProt.compare ("Tcp") == 0)
    {
        Config::SetDefault ("ns3::TcpSocketBase::Target", BooleanValue (false));
    }
    else if (transportProt.compare ("DcTcp") == 0)
    {
        NS_LOG_INFO ("Enabling DcTcp");
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpDCTCP::GetTypeId ()));
    }
    else
    {
        return 0;
    }

    if (aqmStr.compare ("TCN") == 0)
    {
        aqm = TCN;
    }
    else if (aqmStr.compare ("ECNSharp") == 0)
    {
        aqm = ECNSharp;
    }
    else
    {
        return 0;
    }

    // TCP Configuration
    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(1400));
    Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (0));
    Config::SetDefault ("ns3::TcpSocket::ConnTimeout", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
    Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (5)));
    Config::SetDefault ("ns3::TcpSocketBase::ClockGranularity", TimeValue (MicroSeconds (100)));
    Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (40)));
    Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (160000000));
    Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (160000000));

    // TCN Configuration
    // Config::SetDefault ("ns3::TCNQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
    // Config::SetDefault ("ns3::TCNQueueDisc::MaxPackets", UintegerValue (TotalBufferSize));
    // Config::SetDefault ("ns3::TCNQueueDisc::Threshold", TimeValue (MicroSeconds (TCNThreshold)));

    // BFS Configuration
    Config::SetDefault ("ns3::BFSQueueDisc::Mode", StringValue ("QUEUE_MODE_BYTES"));
    Config::SetDefault ("ns3::BFSQueueDisc::MaxBytes", UintegerValue (TotalBufferSize));

    NS_LOG_INFO ("Setting up nodes.");
    NodeContainer senders;
    senders.Create (numOfSenders);

    NodeContainer receivers;
    receivers.Create (nPorts);

    NodeContainer switches;
    switches.Create (1);

    InternetStackHelper internet;
    internet.Install (senders);
    internet.Install (switches);
    internet.Install (receivers);

    PointToPointHelper p2p;

    TrafficControlHelper tc;

    NS_LOG_INFO ("Assign IP address");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");

    for (uint32_t i = 0; i < numOfSenders; ++i)
    {
        uint32_t linkLatency = 30;
	// if (i == 2) {
	// 	linkLatency = 40;
    //     } else if (i == 3) {
  	// 	linkLatency = 70;
	// }
        NS_LOG_INFO ("Generate link latency: " << linkLatency);
        p2p.SetChannelAttribute ("Delay", TimeValue (MicroSeconds(linkLatency)));
        p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
        p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (TotalBufferSize));

        NodeContainer nodeContainer = NodeContainer (senders.Get (i), switches.Get (0));
        NetDeviceContainer netDeviceContainer = p2p.Install (nodeContainer);
        Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign (netDeviceContainer);
        ipv4.NewNetwork ();
        tc.Uninstall (netDeviceContainer);
    }
    std::vector<Ptr<DWRRQueueDisc> > dwrrQdiscs(nPorts);
    Ipv4InterfaceContainer switchToRecvIpv4Container[nPorts];




    p2p.SetChannelAttribute ("Delay", TimeValue (MicroSeconds(50)));
    p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
    p2p.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (5));
    for (uint32_t pos = 0;pos<nPorts;pos++)
    {
        NodeContainer switchToRecvNodeContainer = NodeContainer (switches.Get (0), receivers.Get (pos));
        NetDeviceContainer switchToRecvNetDeviceContainer = p2p.Install (switchToRecvNodeContainer);


        ObjectFactory innerQueueFactory;
        innerQueueFactory.SetTypeId ("ns3::BFSQueueDisc");
        // Ptr<QueueDisc> queueDisc = innerQueueFactory.Create<QueueDisc> ();
        // dwrrQdisc->AddDWRRClass (queueDisc, 0, 1500);
        
        Ptr<DWRRQueueDisc> dwrrQdisc = CreateObject<DWRRQueueDisc> ();
        Ptr<Ipv4SimplePacketFilter> filter = CreateObject<Ipv4SimplePacketFilter> ();
        dwrrQdisc->AddPacketFilter (filter);
        
        for(uint32_t queue=0;queue<nQueue;queue++){
            Ptr<QueueDisc> queueDisc = innerQueueFactory.Create<QueueDisc> ();
            // queueDisc->TraceConnectWithoutContext("Drop", MakeBoundCallback(&Drop,dwrrQdiscs, pos,queue));
            dwrrQdisc->AddDWRRClass (queueDisc, queue, dwrr_quantum[queue]);
        }

        Ptr<NetDevice> device0 = switchToRecvNetDeviceContainer.Get (0);
        dwrrQdisc->SetNetDevice (device0);
        Ptr<TrafficControlLayer> tcl0 = device0->GetNode ()->GetObject<TrafficControlLayer> ();
        tcl0->SetRootQueueDiscOnDevice (device0, dwrrQdisc); 
        dwrrQdiscs[pos]=dwrrQdisc;

        // Ptr<QueueDisc> queueDisc1 = innerQueueFactory.Create<QueueDisc> ();
        // Ptr<NetDevice> device1 = switchToRecvNetDeviceContainer.Get (1);
        // queueDisc1->SetNetDevice (device1);
        // Ptr<TrafficControlLayer> tcl1 = device1->GetNode ()->GetObject<TrafficControlLayer> ();
        // tcl1->SetRootQueueDiscOnDevice (device1, queueDisc1); 

        
        switchToRecvIpv4Container[pos] = ipv4.Assign (switchToRecvNetDeviceContainer);
        ipv4.NewNetwork ();
    }
    for(uint32_t pos=0;pos<nPorts;pos++){
        Ptr<DWRRQueueDisc> dwrrQdisc = dwrrQdiscs[pos];
        for(uint32_t queue=0;queue<nQueue;queue++){
            Ptr<QueueDisc> queueDisc = dwrrQdisc->GetDWRRQueueDisc(queue);
            // queueDisc->TraceConnectWithoutContext("BytesInQueue", MakeBoundCallback(&TcBytesInQueueTrace, dwrrQdiscs, pos,queue));
        }
    }
    // tc.SetRootQueueDisc ("ns3::PfifoFastQueueDisc", "Limit", UintegerValue (TotalBufferSize));

    NS_LOG_INFO ("Setting up routing table");

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


    NS_LOG_INFO ("Initialize random seed: " << randomSeed);
    if (randomSeed == 0)
    {
        srand ((unsigned)time (NULL));
    }
    else
    {
        srand (randomSeed);
    }

    uint16_t basePort = 8080;

    NS_LOG_INFO ("Install 3 large TCP flows");
    uint32_t recv=0;
    for (uint32_t i = 0; i < 3; ++i)
    {
        // if(i%2==0){
        //     recv=1;
        // }else{
        //     recv=0;
        // }
        BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (switchToRecvIpv4Container[recv].GetAddress (1), basePort));
        source.SetAttribute ("MaxBytes", UintegerValue (0)); // 150kb
        source.SetAttribute ("SendSize", UintegerValue (1400));
        source.SetAttribute ("SimpleTOS", UintegerValue (i));
        ApplicationContainer sourceApps = source.Install (senders.Get (i));
        sourceApps.Start (Seconds (0.1));
        sourceApps.Stop (Seconds (endTime));

        PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), basePort++));
        ApplicationContainer sinkApp = sink.Install (receivers.Get (recv));
        sinkApp.Start (Seconds (0.0));
        sinkApp.Stop (Seconds (endTime));
        Ptr<PacketSink> pktSink = sinkApp.Get (0)->GetObject<PacketSink> ();
        Simulator::ScheduleNow (&CheckThroughput, pktSink, i, recv);
    }


    // NS_LOG_INFO ("Install 100 short TCP flows");
    // for (uint32_t i = 0; i < 100; ++i)
    // {
    //     double startTime = rand_range (0.0, 0.4);
    //     uint32_t tos = rand_range (0, 3);
    //     BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (switchToRecvIpv4Container.GetAddress (1), basePort));
    //     source.SetAttribute ("MaxBytes", UintegerValue (28000)); // 14kb
    //     source.SetAttribute ("SendSize", UintegerValue (1400));
    //     source.SetAttribute ("SimpleTOS", UintegerValue (tos));
    //     ApplicationContainer sourceApps = source.Install (senders.Get (2 + i % 2));
    //     sourceApps.Start (Seconds (startTime));
    //     sourceApps.Stop (Seconds (endTime));

    //     PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), basePort++));
    //     ApplicationContainer sinkApp = sink.Install (switchToRecvNodeContainer.Get (1));
    //     sinkApp.Start (Seconds (0.0));
    //     sinkApp.Stop (Seconds (endTime));
    // }

    NS_LOG_INFO ("Enabling Flow Monitor");
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    NS_LOG_INFO ("Run Simulations");

    Simulator::Stop (Seconds (endTime));
    Simulator::Run ();

    flowMonitor->SerializeToXmlFile(GetFormatedStr (id, "Flow_Monitor", "xml", aqm), true, true);

    Simulator::Destroy ();

    return 0;
}
