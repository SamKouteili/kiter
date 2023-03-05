/*
 * kperiodic.cpp
 *
 * Author: jkmingwen
 *
 */

#include <map>
#include <vector>
#include <string>
#include <printers/stdout.h>
#include <commons/verbose.h>
#include <printers/SDF3Wrapper.h> // to write XML files
#include <commons/commons.h>
#include <models/Dataflow.h>
#include <models/EventGraph.h>
#include <algorithms/normalization.h>
#include <models/repetition_vector.h>
#include "buffer_sizing.h"
#include "kperiodic.h"
#include "monotonic_optimisation.h"
#include "base_monotonic_optimisation.h"
#include <chrono> // to take computation timings
#include <boost/filesystem.hpp>

// #define WRITE_GRAPHS // uncomment to write dot files of explored graphs

// Compute and return period and causal dependency cycles of given dataflow graph
kperiodic_result_t algorithms::compute_Kperiodic_throughput_and_cycles(models::Dataflow* const dataflow, parameters_list_t parameters) {
  
    VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");

    bool verbose = false;
    if (parameters.find("PRINT") != parameters.end() ) {
        verbose = true;
    }
    VERBOSE_INFO("Please note you can use the PRINT parameter");

    EXEC_COUNT sumNi = 0;
    EXEC_COUNT sumKi = dataflow->getVerticesCount();

    {ForEachTask(dataflow,t) {
        sumNi += dataflow->getNi(t) ;
    }}

    // STEP 0.1 - PRE
    VERBOSE_ASSERT(dataflow,TXT_NEVER_HAPPEND);
    VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");
    EXEC_COUNT iteration_count = 0;

    // STEP 1 - generate initial vector
    std::map<Vertex,EXEC_COUNT> kvector;
    {ForEachVertex(dataflow,t) {
        kvector[t] = 1;

    }}


    kperiodic_result_t result;



    VERBOSE_INFO("KPeriodic EventGraph generation");

    //STEP 1 - Generate Event Graph
    models::EventGraph* eg = generateKPeriodicEventGraph(dataflow,&kvector);


    VERBOSE_INFO("KPeriodic EventGraph generation Done");

    //STEP 2 - resolve the MCRP on this Event Graph
    std::pair<TIME_UNIT,std::vector<models::EventGraphEdge> > howard_res = eg->MinCycleRatio();

    std::vector<models::EventGraphEdge> * critical_circuit = &(howard_res.second);

    //STEP 3 - convert CC(eg) => CC(graph)
    VERBOSE_DEBUG("Critical circuit is about " << critical_circuit->size() << " edges.");
    for (std::vector<models::EventGraphEdge>::iterator it = critical_circuit->begin() ; it != critical_circuit->end() ; it++ ) {
        VERBOSE_DEBUG("   -> " << eg->getChannelId(*it) << " : " << eg->getSchedulingEvent(eg->getSource(*it)).toString() << " to " <<  eg->getSchedulingEvent(eg->getTarget(*it)).toString() <<  " = (" << eg->getConstraint(*it)._w << "," << eg->getConstraint(*it)._d << ")" );
        ARRAY_INDEX channel_id = eg->getChannelId(*it);
        try {
            Edge        channel    = dataflow->getEdgeById(channel_id);
            result.critical_edges.insert(channel);
        } catch(...) {
            VERBOSE_DEBUG("      is loopback");
        }
    }

    TIME_UNIT frequency = howard_res.first;

    VERBOSE_INFO("KSchedule function get " << frequency << " from MCRP." );
    VERBOSE_INFO("  ->  then omega =  " <<  1 / frequency );

    result.throughput = frequency;

    ////////////// SCHEDULE CALL // END


    if (verbose) {
        std::cout << "Iteration "<< std::fixed << std::setw( 4 ) << iteration_count <<  "      period = "  << std::fixed << std::setw( 15 ) << std::setprecision( 2 ) << 1.0/result.throughput  <<  "      complexity = "  << std::setw( 4 )  << (sumKi * 100) / sumNi << std::endl ;
    }

    if (result.critical_edges.size() != 0) {


        VERBOSE_INFO("1-periodic throughput (" << result.throughput <<  ") is not enough.");
        VERBOSE_INFO("   Critical circuit is " << cc2string(dataflow,&(result.critical_edges)) <<  "");

        while (true) {



            iteration_count++;
            ////////////// SCHEDULE CALL // BEGIN : resultprime = KSchedule(dataflow,&kvector);

            kperiodic_result_t resultprime;

            //VERBOSE_ASSERT( algorithms::normalize(dataflow),"inconsistent graph");
            VERBOSE_INFO("KPeriodic EventGraph generation");

            //STEP 1 - Generate Event Graph and update vector
            if (!updateEventGraph( dataflow ,  &kvector, &(result.critical_edges), eg)) break ;

            VERBOSE_INFO("KPeriodic EventGraph generation Done");

            //STEP 2 - resolve the MCRP on this Event Graph
            std::pair<TIME_UNIT,std::vector<models::EventGraphEdge> > howard_res_bis = eg->MinCycleRatio();

            std::vector<models::EventGraphEdge> * critical_circuit = &(howard_res_bis.second);

            //STEP 3 - convert CC(eg) => CC(graph)
            VERBOSE_DEBUG("Critical circuit is about " << critical_circuit->size() << " edges.");
            for (std::vector<models::EventGraphEdge>::iterator it = critical_circuit->begin() ; it != critical_circuit->end() ; it++ ) {
                VERBOSE_DEBUG("   -> " << eg->getChannelId(*it) << " : " << eg->getSchedulingEvent(eg->getSource(*it)).toString() << " to " <<  eg->getSchedulingEvent(eg->getTarget(*it)).toString() <<  " = (" << eg->getConstraint(*it)._w << "," << eg->getConstraint(*it)._d << ")" );
                ARRAY_INDEX channel_id = eg->getChannelId(*it);
                try {
                    Edge        channel    = dataflow->getEdgeById(channel_id);
                    resultprime.critical_edges.insert(channel);
                } catch(...) {
                    VERBOSE_DEBUG("      is loopback");
                }
            }

            TIME_UNIT frequency = howard_res_bis.first;

            VERBOSE_INFO("KSchedule function get " << frequency << " from MCRP." );
            VERBOSE_INFO("  ->  then omega =  " <<  1 / frequency );

            resultprime.throughput = frequency;

            ////////////// SCHEDULE CALL // END

            if (sameset(dataflow,&(resultprime.critical_edges),&(result.critical_edges)))  {
                VERBOSE_INFO("Critical circuit is the same");
                result = resultprime;

                break;
            }
            result = resultprime;
            VERBOSE_INFO("Current K-periodic throughput (" << result.throughput <<  ") is not enough.");
            VERBOSE_DEBUG("   Critical circuit is " << cc2string(dataflow,&(result.critical_edges)) <<  "");


            if (verbose) {
                sumKi = 0;
                {ForEachVertex(dataflow,t) {
                    sumKi += kvector[t];
                }}

                std::cout << "Iteration "<< std::fixed << std::setw( 4 ) << iteration_count <<  "      period = "  << std::fixed << std::setw( 15 ) << std::setprecision( 2 ) << 1.0/result.throughput  <<  "      complexity = "  << std::setw( 4 )  << (sumKi * 100) / sumNi << std::endl ;
            }

        }

    }


    VERBOSE_INFO( "K-periodic schedule - iterations count is " << iteration_count << "  final size is " << eg->getEventCount() << " events and " << eg->getConstraintsCount() << " constraints.");
    delete eg;

    EXEC_COUNT total_ki = 0;
    {ForEachVertex(dataflow,t) {
        total_ki += kvector[t];
    }}

    VERBOSE_INFO("K-periodic schedule - total_ki=" << sumKi << " total_ni=" << sumNi );
    


    return result;
}


StorageDistributionSet algorithms::compute_Kperiodic_throughput_dse_sd (models::Dataflow* const dataflow,
                                                   parameters_list_t  parameters) {



	  bool writeLogFiles = false;
	  bool isMonoOpt = false;
	  bool isBaseMonoOpt = false;
	  bool thrTargetSpecified = false;
    bool isMaxSet = false;
	  std::string dirName = "./data/"; // default

	  // parse parameters for KDSE
	  if (parameters.find("LOGDIR") != parameters.end()) { // log output of DSE (includes pareto points and all search points)
	    writeLogFiles = true;
	    dirName = parameters["LOGDIR"];
	  }

	  // parse parameters for KDSE
	  // TODO : I would remove this now, only using LOGDIR
	  if (parameters.find("LOG") != parameters.end()) { // log output of DSE (includes pareto points and all search points) NOTE: parent directories need to be created beforehand
	    writeLogFiles = true;
	  }
	  if (parameters.find("M_OPT") != parameters.end()) { // use monotonic optimisation
	    isMonoOpt = true;
	  }
	  if (parameters.find("B_M_OPT") != parameters.end()) { // use base monotonic optimisation
	    isBaseMonoOpt = true;
	  }
	  if (parameters.find("THR") != parameters.end()) { // specify target throughput of DSE
	    thrTargetSpecified = true;
	  } else {
	    VERBOSE_WARNING("No target throughput specified (target throughput will be set to max throughput by default) --- specify target throughput with '-p THR=n' flag");
	  }
    if (parameters.find("MAX_SET") != parameters.end()) { // specify target throughput of DSE
	    isMaxSet = true;
	  }




  TIME_UNIT thrTarget;
  long int computation_counter = 0;
  std::string ppDirName = dirName + "/pp_logs/"; // logs of pareto points
  std::string logDirName = dirName + "/dse_logs/";
  std::string debugXMLName = dirName + "/xmls/";

  if (writeLogFiles) {
	  VERBOSE_ASSERT(boost::filesystem::is_directory(dirName), "Please create the log directory " << dirName << " or specify a different one with the LOGDIR parameter.");  // true, directory exists
	  boost::filesystem::create_directory(ppDirName);
	  boost::filesystem::create_directory(logDirName);
	  // boost::filesystem::create_directory(debugXMLName);
  }


  std::string methodName;
  #ifdef WRITE_GRAPHS
    std::string dotfileDirName = dirName +  "/dotfiles/";
    boost::filesystem::create_directory(dotfileDirName);
  #endif

  // create new graph with modelled bounded channel quantities
  models::Dataflow* dataflow_prime = new models::Dataflow(*dataflow);
  dataflow_prime->reset_computation();
  // add feedback channels in new graph to model bounded channel quantities
  {ForEachEdge(dataflow, c) {
      auto new_edge = dataflow_prime->addEdge(dataflow_prime->getEdgeTarget(c),
                                              dataflow_prime->getEdgeSource(c));
      dataflow_prime->setEdgeInPhases(new_edge,
                                      dataflow_prime->getEdgeOutVector(c));
      dataflow_prime->setEdgeOutPhases(new_edge,
                                       dataflow_prime->getEdgeInVector(c));
      dataflow_prime->setPreload(new_edge, dataflow->getPreload(c));
      dataflow_prime->setEdgeName(new_edge,
                                  dataflow_prime->getEdgeName(c) + "_prime");
    }}

  // initialise search parameters
  std::map<Edge, TOKEN_UNIT> minStepSizes;
  std::map<Edge, std::pair<TOKEN_UNIT, TOKEN_UNIT>> minChannelSizes;
  TOKEN_UNIT minDistributionSize;

  VERBOSE_DSE("INITIALISING SEARCH PARAMETERS:" << std::endl);
  initSearchParameters(dataflow_prime,
                       minStepSizes,
                       minChannelSizes);

  /* Setting lower bound channel quantities:
     In order to model bounded channel sizes, we make an edge
     that feeds back from the destination node to the source node. 
     The capacity of a channel is thus modelled by: 
     - INITIAL TOKENS, for original edges and
     - CHANNEL QUANTITY - INITIAL TOKENS, for feedback edges 
     where the first element in the pair of minChannelSizes is the 
     initial tokens in the channel while the second element is the 
     capacity of the channel (in tokens). */
  {ForEachEdge(dataflow_prime, c) {
      if (dataflow_prime->getEdgeId(c) <= dataflow->getEdgesCount()) {
        minChannelSizes[c].second = 0; // original edges must have capacity 0
      }
      minChannelSizes[c].first = dataflow_prime->getPreload(c);
   }}

  minDistributionSize = findMinimumDistributionSz(minChannelSizes);
  
  // initialise and store initial storage distribution state
  StorageDistribution initDist(dataflow_prime->getEdgesCount(),
                               0,
                               minChannelSizes,
                               minDistributionSize);
  
  // initialise modelled graph with lower bound distribution
  {ForEachEdge(dataflow_prime, c) {
      if (dataflow_prime->getEdgeId(c) <= dataflow->getEdgesCount()) { // original channel IDs
        dataflow_prime->setPreload(c, initDist.getInitialTokens(c));
      } else {
        // subtract initial tokens from buffer size to model any initial tokens in buffer
        dataflow_prime->setPreload(c, (initDist.getChannelQuantity(c) -
                                       initDist.getInitialTokens(c)));
      }
  }}

  // uncomment code block to get XMLs of lower bound distribution
  // printers::writeSDF3File(dirName + "dse_min_distribution_" +
  //                        dataflow_prime->getGraphName() + "_kiter"
  //                        ".xml", dataflow_prime);

  // calculate max throughput and current throughput with lower bound distribution
  kperiodic_result_t result_max = compute_Kperiodic_throughput_and_cycles(dataflow, parameters);
  VERBOSE_DSE("Max throughput: " << result_max.throughput << std::endl);
  if (!thrTargetSpecified) {
    thrTarget = result_max.throughput;
    VERBOSE_DSE("Target throughput set to max of " << thrTarget << std::endl);
  } else { // target throughput specified
    thrTarget = std::stold(parameters.find("THR")->second);
    if (thrTarget <= result_max.throughput) {
      VERBOSE_DSE("Target throughput set to " << thrTarget << std::endl);
    } else { // invalid target throughput
      std::cerr << "ERROR: Specified target throughput (" << thrTarget
                << ") is larger than maximum throughput (" << result_max.throughput
                << ")" << std::endl;

      StorageDistributionSet temp; // NOTE just a workaround for now
      return temp; // TODO need to properly define storage distribution set here
    }
  }
  kperiodic_result_t result = compute_Kperiodic_throughput_and_cycles(dataflow_prime, parameters);

  /* a negative throughput indicates a deadlocked graph and so 
     we set it to 0 to avoid bugs from having a negative throughput */
  if (result.throughput < 0) {
    initDist.setThroughput(0);
  } else { // set throughput to one computed from initial distribution otherwise
    initDist.setThroughput(result.throughput);
  }

  // add initial distribution to list of storage distributions
  StorageDistributionSet checklist;
  std::chrono::duration<double, std::milli> cumulativeTime; // store timings
  if (isMonoOpt) {
    methodName = "_m_opt";
    auto startTime = std::chrono::steady_clock::now();
    checklist = algorithms::monotonic_optimised_Kperiodic_throughput_dse(dataflow_prime,
                                                                         initDist,
                                                                         thrTarget,
                                                                         computation_counter,
                                                                         parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    std::cout << "M_OPT: time taken: " << cumulativeTime.count() << std::endl;
  } else if (isBaseMonoOpt) {
    methodName = "_base_m_opt";
    auto startTime = std::chrono::steady_clock::now();
    checklist = algorithms::base_monotonic_optimised_Kperiodic_throughput_dse(dataflow_prime,
                                                                              initDist,
                                                                              thrTarget,
                                                                              computation_counter,
                                                                              parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    std::cout << "B_M_OPT: time taken: " << cumulativeTime.count() << std::endl;
  } else {
    methodName = "_kiter";
    checklist = StorageDistributionSet(initDist.getDistributionSize(),
                                       initDist);
  }


  /* Initialise set of minimal storage distributions
     (a set of pairs of throughput and storage distribution) 
     --- this will store the results of our DSE */
  StorageDistributionSet minStorageDist;
  StorageDistributionSet maxStorageDist; // for SDs with thr>=targetThr
  
  // initialise data logging file
  std::ofstream dseLog;
  if (writeLogFiles) {
    dseLog.open(logDirName + dataflow_prime->getGraphName() + "_dselog" + methodName + ".csv");
    dseLog << "storage distribution size,throughput,channel quantities,dependency mask,computation duration,cumulative duration"
           << std::endl; // initialise headers
  } else {
	    std::cout << "storage distribution size,throughput,channel quantities,computation duration,cumulative duration"
	           << std::endl; // initialise headers
  }
  
  // Start search algorithm
  VERBOSE_DSE("\n");
  VERBOSE_DSE("DSE BEGIN:" << std::endl);
  
  while (!minStorageDist.isSearchComplete(checklist, thrTarget)) {
    VERBOSE_DSE("Checking next storage distribution in checklist --- current checklist size: "
                << checklist.getSize() << std::endl);
    StorageDistribution checkDist(checklist.getNextDistribution()); // copy distribution for checking (first in checklist)
    checklist.removeStorageDistribution(checklist.getNextDistribution()); // remove said storage distribution from checklist
    
    // Update graph with storage distribution just removed from checklist
    VERBOSE_DSE(std::endl);
    VERBOSE_DSE("Exploring new storage distribution: " << std::endl);
    dataflow_prime->reset_computation(); // make graph writeable to alter channel size
    {ForEachEdge(dataflow_prime, c) {
        if (dataflow_prime->getEdgeId(c) > dataflow->getEdgesCount()) { // only modelled buffer preloads change
          dataflow_prime->setPreload(c, (checkDist.getChannelQuantity(c) -
                                         checkDist.getInitialTokens(c))); // always account for initial tokens in buffer
        }
      }}
    // UNCOMMENT TO WRITE XMLs OF EXPLORED GRAPHS
    // commons::writeSDF3File(debugXMLName + "dse_min_distribution_" +
    // 			   dataflow_prime->getGraphName() + "_kiter" + std::to_string(computation_counter) +
    // 			   ".xml", dataflow_prime);

    // Compute throughput and storage deps
    auto startTime = std::chrono::steady_clock::now();
    result = compute_Kperiodic_throughput_and_cycles(dataflow_prime, parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    computation_counter++;

    if (result.throughput < 0) { // all deadlocked graphs are equal in terms of throughput
      checkDist.setThroughput(0);
    } else {
      checkDist.setThroughput(result.throughput);
    }
    VERBOSE_DSE(checkDist.printInfo(dataflow_prime));

    // write current storage distribution info to DSE log
    if (writeLogFiles) {
      dseLog << checkDist.getDistributionSize() << ","
             << checkDist.getThroughput() << ","
             << checkDist.print_quantities_csv(dataflow_prime) << ","
             << checkDist.print_dependency_mask(dataflow_prime, result) << ","
             << execTime.count() << ","
             << cumulativeTime.count() << std::endl;
    } else {
        std::cout << checkDist.getDistributionSize() << ","
               << checkDist.getThroughput() << ","
               << checkDist.print_quantities_csv(dataflow_prime) << ","
               << execTime.count() << ","
               << cumulativeTime.count() << std::endl;
    }

    // Add storage distribution and computed throughput to set of minimal storage distributions
    VERBOSE_DSE("\n");
    VERBOSE_DSE("\tUpdating set of minimal storage distributions..."
                << std::endl);
    minStorageDist.addStorageDistribution(checkDist);
    if (result.throughput >= thrTarget){
      maxStorageDist.addStorageDistribution(checkDist);
    }
    VERBOSE_DSE(std::endl);

    // Create new storage distributions for every storage dependency found; add new storage distributions to checklist
    for (std::set<Edge>::iterator it = (result.critical_edges).begin();
         it != (result.critical_edges).end(); it++) {
      StorageDistribution newDist(checkDist);
      // only increase channel quantity on "modelled" channels
      if (dataflow_prime->getEdgeId(*it) > dataflow->getEdgesCount()) {
        VERBOSE_DSE("\tFound storage dependency in channel "
                    << dataflow_prime->getEdgeName(*it) << std::endl);
        // make new modelled storage distribution according to storage dependencies
        newDist.setChannelQuantity(*it, (newDist.getChannelQuantity(*it) +
                                         minStepSizes[*it]));
        VERBOSE_DSE("\t\tIncreasing channel size of "
                    << dataflow_prime->getEdgeName(*it) << " to "
                    << newDist.getChannelQuantity(*it) << std::endl);
        VERBOSE_DSE("\tUpdating checklist with new storage distribution..."
                    << std::endl);
        checklist.addStorageDistribution(newDist);
      }
    }

    // Ensure minimal set is indeed minimal
    VERBOSE_DSE("\tTrying to minimise set with distribution size: "
                << checkDist.getDistributionSize() << std::endl);
    minStorageDist.minimizeStorageDistributions(checkDist);
    maxStorageDist.minimizeStorageDistributions(checkDist);
    VERBOSE_DSE(std::endl);
  }

  // The minimum storage distribution for a throughput of 0 is (0, 0,..., 0)
  if (minStorageDist.getNextDistribution().getThroughput() == 0) {
    StorageDistribution zeroDist(minStorageDist.getNextDistribution());
    {ForEachEdge(dataflow_prime, c) {
        zeroDist.setChannelQuantity(c, 0);
      }}
    minStorageDist.removeStorageDistribution(minStorageDist.getNextDistribution());
    minStorageDist.addStorageDistribution(zeroDist);
  }
  VERBOSE_DSE("\n");
  VERBOSE_DSE("DSE RESULTS [START] (target throughput: " << thrTarget
              << "):" << std::endl);
  VERBOSE_DSE("\n" << minStorageDist.printDistributions(dataflow_prime));
  VERBOSE_DSE("DSE RESULTS [END]" << std::endl);
  VERBOSE_DSE("Done with search!" << std::endl);
  VERBOSE_DSE("Number of computations: " << computation_counter << std::endl);
  VERBOSE_DSE("Number of pareto points: " << minStorageDist.getSize() << std::endl);

  // Write log files and print file paths
  if (writeLogFiles) {
    dseLog.close();
    std::cout << "\nDSE log has been written to: "
              << logDirName + dataflow_prime->getGraphName() + "_dselog" + methodName + ".csv"
              << std::endl;
    minStorageDist.writeCSV(ppDirName + dataflow_prime->getGraphName() +
                            "_pp" + methodName + ".csv", dataflow_prime);
    std::cout << "\nPareto points have been written to: "
              << ppDirName + dataflow_prime->getGraphName() + "_pp" + methodName + ".csv"
              << std::endl;

    #ifdef WRITE_GRAPHS
    minStorageDist.printGraphs(dataflow_prime,
                               dirName + dotfileDirName);
    std::cout << "\n" + std::to_string(minStorageDist.getSize()) + " "
              << "graphs of minimal storage distributions written to: "
              << dirName + dotfileDirName + dataflow_prime->getGraphName() + "_n.dot"
              << std::endl;
    #endif
  } else {
    std::cout << "\nNote that you can use flag '-p LOG=true' or '-p LOGDIR=/whereyougo/' to write logs of DSE"
              << std::endl;
  }

  if (isMaxSet) {
    return maxStorageDist; // only return SDs with thr >= targetThr
  } else {
    return minStorageDist;
  }
}

void algorithms::compute_Kperiodic_throughput_dse (models::Dataflow* const dataflow,
                                                   parameters_list_t  parameters) {

    StorageDistributionSet sd = compute_Kperiodic_throughput_dse_sd(dataflow, parameters);
    
  }




/***********************************************************************************/

void algorithms::mod_Kperiodic_throughput_dse (models::Dataflow* const dataflow,
                                                   parameters_list_t  parameters) {



	  bool writeLogFiles = false;
	  bool isMonoOpt = false;
	  bool isBaseMonoOpt = false;
	  bool thrTargetSpecified = false;
	  std::string dirName = "./data/"; // default

	  // parse parameters for KDSE
	  if (parameters.find("LOGDIR") != parameters.end()) { // log output of DSE (includes pareto points and all search points)
	    writeLogFiles = true;
	    dirName = parameters["LOGDIR"];
	  }

	  // parse parameters for KDSE
	  // TODO : I would remove this now, only using LOGDIR
	  if (parameters.find("LOG") != parameters.end()) { // log output of DSE (includes pareto points and all search points) NOTE: parent directories need to be created beforehand
	    writeLogFiles = true;
	  }
	  if (parameters.find("M_OPT") != parameters.end()) { // use monotonic optimisation
	    isMonoOpt = true;
	  }
	  if (parameters.find("B_M_OPT") != parameters.end()) { // use base monotonic optimisation
	    isBaseMonoOpt = true;
	  }
	  if (parameters.find("THR") != parameters.end()) { // specify target throughput of DSE
	    thrTargetSpecified = true;
	  } else {
	    VERBOSE_WARNING("No target throughput specified (target throughput will be set to max throughput by default) --- specify target throughput with '-p THR=n' flag");
	  }




  TIME_UNIT thrTarget;
  long int computation_counter = 0;
  std::string ppDirName = dirName + "/pp_logs/"; // logs of pareto points
  std::string logDirName = dirName + "/dse_logs/";
  std::string debugXMLName = dirName + "/xmls/";

  if (writeLogFiles) {
	  VERBOSE_ASSERT(boost::filesystem::is_directory(dirName), "Please create the log directory " << dirName << " or specify a different one with the LOGDIR parameter.");  // true, directory exists
	  boost::filesystem::create_directory(ppDirName);
	  boost::filesystem::create_directory(logDirName);
	  // boost::filesystem::create_directory(debugXMLName);
  }


  std::string methodName;
#ifdef WRITE_GRAPHS
  std::string dotfileDirName = dirName +  "/dotfiles/";
  boost::filesystem::create_directory(dotfileDirName);
#endif

  // create new graph with modelled bounded channel quantities
  models::Dataflow* dataflow_prime = new models::Dataflow(*dataflow);
  // add feedback channels in new graph to model bounded channel quantities
  {ForEachEdge(dataflow, c) {
      auto new_edge = dataflow_prime->addEdge(dataflow_prime->getEdgeTarget(c),
                                              dataflow_prime->getEdgeSource(c));
      dataflow_prime->setEdgeInPhases(new_edge,
                                      dataflow_prime->getEdgeOutVector(c));
      dataflow_prime->setEdgeOutPhases(new_edge,
                                       dataflow_prime->getEdgeInVector(c));
      dataflow_prime->setPreload(new_edge, dataflow->getPreload(c));
      dataflow_prime->setEdgeName(new_edge,
                                  dataflow_prime->getEdgeName(c) + "_prime");
    }}

  // initialise search parameters
  std::map<Edge, TOKEN_UNIT> minStepSizes;
  std::map<Edge, std::pair<TOKEN_UNIT, TOKEN_UNIT>> minChannelSizes;
  TOKEN_UNIT minDistributionSize;

  VERBOSE_DSE("INITIALISING SEARCH PARAMETERS:" << std::endl);
  initSearchParameters(dataflow_prime,
                       minStepSizes,
                       minChannelSizes);

  /* Setting lower bound channel quantities:
     In order to model bounded channel sizes, we make an edge
     that feeds back from the destination node to the source node. 
     The capacity of a channel is thus modelled by: 
     - INITIAL TOKENS, for original edges and
     - CHANNEL QUANTITY - INITIAL TOKENS, for feedback edges 
     where the first element in the pair of minChannelSizes is the 
     initial tokens in the channel while the second element is the 
     capacity of the channel (in tokens). */
  {ForEachEdge(dataflow_prime, c) {
      if (dataflow_prime->getEdgeId(c) <= dataflow->getEdgesCount()) {
        minChannelSizes[c].second = 0; // original edges must have capacity 0
      }
      minChannelSizes[c].first = dataflow_prime->getPreload(c);
   }}

  minDistributionSize = findMinimumDistributionSz(minChannelSizes);
  
  // initialise and store initial storage distribution state
  StorageDistribution initDist(dataflow_prime->getEdgesCount(),
                               0,
                               minChannelSizes,
                               minDistributionSize);
  
  // initialise modelled graph with lower bound distribution
  {ForEachEdge(dataflow_prime, c) {
      if (dataflow_prime->getEdgeId(c) <= dataflow->getEdgesCount()) { // original channel IDs
        dataflow_prime->setPreload(c, initDist.getInitialTokens(c));
      } else {
        // subtract initial tokens from buffer size to model any initial tokens in buffer
        dataflow_prime->setPreload(c, (initDist.getChannelQuantity(c) -
                                       initDist.getInitialTokens(c)));
      }
  }}

  // uncomment code block to get XMLs of lower bound distribution
  // printers::writeSDF3File(dirName + "dse_min_distribution_" +
  //                        dataflow_prime->getGraphName() + "_kiter"
  //                        ".xml", dataflow_prime);

  // calculate max throughput and current throughput with lower bound distribution
  kperiodic_result_t result_max = compute_Kperiodic_throughput_and_cycles(dataflow, parameters);
  VERBOSE_DSE("Max throughput: " << result_max.throughput << std::endl);
  if (!thrTargetSpecified) {
    thrTarget = result_max.throughput;
    VERBOSE_DSE("Target throughput set to max of " << thrTarget << std::endl);
  } else { // target throughput specified
    thrTarget = std::stold(parameters.find("THR")->second);
    if (thrTarget <= result_max.throughput) {
      VERBOSE_DSE("Target throughput set to " << thrTarget << std::endl);
    } else { // invalid target throughput
      std::cerr << "ERROR: Specified target throughput (" << thrTarget
                << ") is larger than maximum throughput (" << result_max.throughput
                << ")" << std::endl;
      return;
    }
  }
  kperiodic_result_t result = compute_Kperiodic_throughput_and_cycles(dataflow_prime, parameters);

  /* a negative throughput indicates a deadlocked graph and so 
     we set it to 0 to avoid bugs from having a negative throughput */
  if (result.throughput < 0) {
    initDist.setThroughput(0);
  } else { // set throughput to one computed from initial distribution otherwise
    initDist.setThroughput(result.throughput);
  }

  // add initial distribution to list of storage distributions
  StorageDistributionSet checklist;
  std::chrono::duration<double, std::milli> cumulativeTime; // store timings
  if (isMonoOpt) {
    methodName = "_m_opt";
    auto startTime = std::chrono::steady_clock::now();
    checklist = algorithms::monotonic_optimised_Kperiodic_throughput_dse(dataflow_prime,
                                                                         initDist,
                                                                         thrTarget,
                                                                         computation_counter,
                                                                         parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    std::cout << "M_OPT: time taken: " << cumulativeTime.count() << std::endl;
  } else if (isBaseMonoOpt) {
    methodName = "_base_m_opt";
    auto startTime = std::chrono::steady_clock::now();
    checklist = algorithms::base_monotonic_optimised_Kperiodic_throughput_dse(dataflow_prime,
                                                                              initDist,
                                                                              thrTarget,
                                                                              computation_counter,
                                                                              parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    std::cout << "B_M_OPT: time taken: " << cumulativeTime.count() << std::endl;
  } else {
    methodName = "_kiter";
    checklist = StorageDistributionSet(initDist.getDistributionSize(),
                                       initDist);
  }


  /* Initialise set of minimal storage distributions
     (a set of pairs of throughput and storage distribution) 
     --- this will store the results of our DSE */
  StorageDistributionSet minStorageDist;
  
  // initialise data logging file
  std::ofstream dseLog;
  if (writeLogFiles) {
    dseLog.open(logDirName + dataflow_prime->getGraphName() + "_dselog" + methodName + ".csv");
    dseLog << "storage distribution size,throughput,channel quantities,dependency mask,computation duration,cumulative duration"
           << std::endl; // initialise headers
  } else {
	    std::cout << "storage distribution size,throughput,channel quantities,computation duration,cumulative duration"
	           << std::endl; // initialise headers
  }
  
  // Start search algorithm
  VERBOSE_DSE("\n");
  VERBOSE_DSE("DSE BEGIN:" << std::endl);
  
  while (!minStorageDist.isSearchComplete(checklist, thrTarget)) {
    VERBOSE_DSE("Checking next storage distribution in checklist --- current checklist size: "
                << checklist.getSize() << std::endl);
    StorageDistribution checkDist(checklist.getNextDistribution()); // copy distribution for checking (first in checklist)
    checklist.removeStorageDistribution(checklist.getNextDistribution()); // remove said storage distribution from checklist
    
    // Update graph with storage distribution just removed from checklist
    VERBOSE_DSE(std::endl);
    VERBOSE_DSE("Exploring new storage distribution: " << std::endl);
    dataflow_prime->reset_computation(); // make graph writeable to alter channel size
    {ForEachEdge(dataflow_prime, c) {
        if (dataflow_prime->getEdgeId(c) > dataflow->getEdgesCount()) { // only modelled buffer preloads change
          dataflow_prime->setPreload(c, (checkDist.getChannelQuantity(c) -
                                         checkDist.getInitialTokens(c))); // always account for initial tokens in buffer
        }
      }}
    // UNCOMMENT TO WRITE XMLs OF EXPLORED GRAPHS
    // commons::writeSDF3File(debugXMLName + "dse_min_distribution_" +
    // 			   dataflow_prime->getGraphName() + "_kiter" + std::to_string(computation_counter) +
    // 			   ".xml", dataflow_prime);

    // Compute throughput and storage deps
    auto startTime = std::chrono::steady_clock::now();
    result = compute_Kperiodic_throughput_and_cycles(dataflow_prime, parameters);
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> execTime = endTime - startTime; // duration in ms
    cumulativeTime += execTime;
    computation_counter++;

    if (result.throughput < 0) { // all deadlocked graphs are equal in terms of throughput
      checkDist.setThroughput(0);
    } else {
      checkDist.setThroughput(result.throughput);
    }
    VERBOSE_DSE(checkDist.printInfo(dataflow_prime));

    // write current storage distribution info to DSE log
    if (writeLogFiles) {
      dseLog << checkDist.getDistributionSize() << ","
             << checkDist.getThroughput() << ","
             << checkDist.print_quantities_csv(dataflow_prime) << ","
             << checkDist.print_dependency_mask(dataflow_prime, result) << ","
             << execTime.count() << ","
             << cumulativeTime.count() << std::endl;
    } else {
        std::cout << checkDist.getDistributionSize() << ","
               << checkDist.getThroughput() << ","
               << checkDist.print_quantities_csv(dataflow_prime) << ","
               << execTime.count() << ","
               << cumulativeTime.count() << std::endl;
    }

    // Add storage distribution and computed throughput to set of minimal storage distributions
    VERBOSE_DSE("\n");
    VERBOSE_DSE("\tUpdating set of minimal storage distributions..."
                << std::endl);
    minStorageDist.addStorageDistribution(checkDist);
    VERBOSE_DSE(std::endl);

    /*
    1. Generate new dataflow of critical cycle
    2. Compute new buffer sizings for cc which exhibits local improvement
      a) need compute function that returns graph rather than void

    3. Ammend critical cycle in original dataflow with new buffer sizings
    4. Add new critical cycle to search space

    */
    models::Dataflow* cc_g = new models::Dataflow(); /***@@@*****/
    std::cout << "Critical edges found:" << std::endl;
    for (std::set<Edge>::iterator it = (result.critical_edges).begin();
         it != (result.critical_edges).end(); it++){
        
        Vertex src = cc_g->addVertex(dataflow_prime->getVertexId(dataflow_prime->getEdgeSource(*it)));
        Vertex trg = cc_g->addVertex(dataflow_prime->getVertexId(dataflow_prime->getEdgeTarget(*it)));
        Edge e = cc_g->addEdge(src, trg, dataflow_prime->getEdgeId(*it), dataflow_prime->getEdgeName(*it));
        cc_g->setPreload(e, dataflow_prime->getPreload(*it)); 
        std::cout << "Source -> Destination: " << src << " -> " << thrTargetSpecified << std::endl;
        std::cout << "\tEdge id: " << dataflow_prime->getEdgeId(*it) << std::endl;
        std::cout << "\tEdge name: " << dataflow_prime->getEdgeName(*it) << std::endl;
        std::cout << "\tEdge Load: " << dataflow_prime->getPreload(*it) << std::endl;
        

    }
    // does this take into account preload? it should because of usecase in dse
    kperiodic_result_t  cc_res = compute_Kperiodic_throughput_and_cycles(cc_g, parameters);
    parameters_list_t new_params(parameters);
    // STEP 1: Check next after; write unit test with jaime's function
    // STEP 2: Check if max_set is returning the maximal set
    new_params["THR"] = std::to_string(std::nextafter(cc_res.throughput, cc_res.throughput+1));
    new_params["MAX_SET"] = "t";
    
    StorageDistributionSet cc_new_sd = compute_Kperiodic_throughput_dse_sd(cc_g, new_params);
    /* Iterate through new storage distribution to add to copy of dataflow_prime */
    /* Add this graph to search space */
    for (std::pair<TOKEN_UNIT, std::vector<StorageDistribution>> cc_pair : cc_new_sd.getSet()){
      /* Maybe check here if token size is same as previous one? better yet check if each
      individual channel has actually grown */
      for (StorageDistribution cc_sd : cc_pair.second){
        StorageDistribution newDist(checkDist);
        // update cc edge quantities and add to distribution
        for (Edge it : cc_sd.getEdges()) {
            if (dataflow_prime->getEdgeId(it) > dataflow->getEdgesCount()) {
          VERBOSE_DSE("\tFound storage dependency in channel "
                      << dataflow_prime->getEdgeName(it) << std::endl);
          // make new modelled storage distribution according to storage dependencies
          newDist.setChannelQuantity(it, cc_sd.getChannelQuantity(it));
          VERBOSE_DSE("\t\tIncreasing channel size of "
                      << dataflow_prime->getEdgeName(it) << " to "
                      << newDist.getChannelQuantity(it) << std::endl);
          VERBOSE_DSE("\tUpdating checklist with new storage distribution..."
                      << std::endl);
        }
          
        checklist.addStorageDistribution(newDist);

        }
      }
    }

    // // Create new storage distributions for every storage dependency found; add new storage distributions to checklist
    // for (std::set<Edge>::iterator it = (result.critical_edges).begin();
    //      it != (result.critical_edges).end(); it++) {
    //   StorageDistribution newDist(checkDist);
    //   // only increase channel quantity on "modelled" channels
    //   if (dataflow_prime->getEdgeId(*it) > dataflow->getEdgesCount()) {
    //     VERBOSE_DSE("\tFound storage dependency in channel "
    //                 << dataflow_prime->getEdgeName(*it) << std::endl);
    //     // make new modelled storage distribution according to storage dependencies
    //     newDist.setChannelQuantity(*it, (newDist.getChannelQuantity(*it) +
    //                                      minStepSizes[*it]));
    //     VERBOSE_DSE("\t\tIncreasing channel size of "
    //                 << dataflow_prime->getEdgeName(*it) << " to "
    //                 << newDist.getChannelQuantity(*it) << std::endl);
    //     VERBOSE_DSE("\tUpdating checklist with new storage distribution..."
    //                 << std::endl);
    //     checklist.addStorageDistribution(newDist);
    //   }
    // }

    // Ensure minimal set is indeed minimal
    VERBOSE_DSE("\tTrying to minimise set with distribution size: "
                << checkDist.getDistributionSize() << std::endl);
    minStorageDist.minimizeStorageDistributions(checkDist);
    VERBOSE_DSE(std::endl);
  }

  // The minimum storage distribution for a throughput of 0 is (0, 0,..., 0)
  if (minStorageDist.getNextDistribution().getThroughput() == 0) {
    StorageDistribution zeroDist(minStorageDist.getNextDistribution());
    {ForEachEdge(dataflow_prime, c) {
        zeroDist.setChannelQuantity(c, 0);
      }}
    minStorageDist.removeStorageDistribution(minStorageDist.getNextDistribution());
    minStorageDist.addStorageDistribution(zeroDist);
  }
  VERBOSE_DSE("\n");
  VERBOSE_DSE("DSE RESULTS [START] (target throughput: " << thrTarget
              << "):" << std::endl);
  VERBOSE_DSE("\n" << minStorageDist.printDistributions(dataflow_prime));
  VERBOSE_DSE("DSE RESULTS [END]" << std::endl);
  VERBOSE_DSE("Done with search!" << std::endl);
  VERBOSE_DSE("Number of computations: " << computation_counter << std::endl);
  VERBOSE_DSE("Number of pareto points: " << minStorageDist.getSize() << std::endl);
  
  // Write log files and print file paths
  if (writeLogFiles) {
    dseLog.close();
    std::cout << "\nDSE log has been written to: "
              << logDirName + dataflow_prime->getGraphName() + "_dselog" + methodName + ".csv"
              << std::endl;
    minStorageDist.writeCSV(ppDirName + dataflow_prime->getGraphName() +
                            "_pp" + methodName + ".csv", dataflow_prime);
    std::cout << "\nPareto points have been written to: "
              << ppDirName + dataflow_prime->getGraphName() + "_pp" + methodName + ".csv"
              << std::endl;

    #ifdef WRITE_GRAPHS
    minStorageDist.printGraphs(dataflow_prime,
                               dirName + dotfileDirName);
    std::cout << "\n" + std::to_string(minStorageDist.getSize()) + " "
              << "graphs of minimal storage distributions written to: "
              << dirName + dotfileDirName + dataflow_prime->getGraphName() + "_n.dot"
              << std::endl;
    #endif
  } else {
    std::cout << "\nNote that you can use flag '-p LOG=true' or '-p LOGDIR=/whereyougo/' to write logs of DSE"
              << std::endl;
  }
}


