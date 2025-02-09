/*
 * nperiodic.cpp
 *
 *  Created on: 5 juil. 2013
 *      Author: toky
 */


#include <vector>
#include <commons/verbose.h>
#include <models/Dataflow.h>
#include <models/EventGraph.h>
#include <algorithms/normalization.h>
#include <algorithms/nperiodic.h>
#include <algorithms/throughput/kperiodic.h>
#include <lp/glpsol.h>


std::map<Vertex,EXEC_COUNT> algorithms::get_Kvector(models::Dataflow *  const dataflow ) {

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
	VERBOSE_KPERIODIC_DEBUG("Critical circuit is about " << critical_circuit->size() << " edges.");
	for (std::vector<models::EventGraphEdge>::iterator it = critical_circuit->begin() ; it != critical_circuit->end() ; it++ ) {
		VERBOSE_KPERIODIC_DEBUG("   -> " << eg->getChannelId(*it) << " : " << eg->getSchedulingEvent(eg->getSource(*it)).toString() << " to " <<  eg->getSchedulingEvent(eg->getTarget(*it)).toString() <<  " = (" << eg->getConstraint(*it)._w << "," << eg->getConstraint(*it)._d << ")" );
		ARRAY_INDEX channel_id = eg->getChannelId(*it);
		try {
			Edge        channel    = dataflow->getEdgeById(channel_id);
			result.critical_edges.insert(channel);
		} catch(...) {
			VERBOSE_KPERIODIC_DEBUG("      is loopback");
		}
	}

	TIME_UNIT frequency = howard_res.first;

	VERBOSE_INFO("KSchedule function get " << frequency << " from MCRP." );
	VERBOSE_INFO("  ->  then omega =  " <<  1 / frequency );

	result.throughput = frequency;

	////////////// SCHEDULE CALL // END

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
			VERBOSE_KPERIODIC_DEBUG("Critical circuit is about " << critical_circuit->size() << " edges.");
			for (std::vector<models::EventGraphEdge>::iterator it = critical_circuit->begin() ; it != critical_circuit->end() ; it++ ) {
				VERBOSE_KPERIODIC_DEBUG("   -> " << eg->getChannelId(*it) << " : " << eg->getSchedulingEvent(eg->getSource(*it)).toString() << " to " <<  eg->getSchedulingEvent(eg->getTarget(*it)).toString() <<  " = (" << eg->getConstraint(*it)._w << "," << eg->getConstraint(*it)._d << ")" );
				ARRAY_INDEX channel_id = eg->getChannelId(*it);
				try {
					Edge        channel    = dataflow->getEdgeById(channel_id);
					resultprime.critical_edges.insert(channel);
				} catch(...) {
					VERBOSE_KPERIODIC_DEBUG("      is loopback");
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
			VERBOSE_KPERIODIC_DEBUG("   Critical circuit is " << cc2string(dataflow,&(result.critical_edges)) <<  "");
		}

	}


	VERBOSE_INFO( "K-periodic schedule - iterations count is " << iteration_count << "  final size is " << eg->getEventCount() << " events and " << eg->getConstraintsCount() << " constraints.");
	delete eg;

	EXEC_COUNT total_ni = 0;
	EXEC_COUNT total_ki = 0;
	{ForEachVertex(dataflow,t) {
		total_ni += dataflow->getNi(t);
		total_ki += kvector[t];
	}}

	VERBOSE_INFO("K-periodic schedule - total_ki=" << total_ki << " total_ni=" << total_ni );
	return kvector;
}


void algorithms::print_Nperiodic_eventgraph (models::Dataflow* const  dataflow, parameters_list_t) {


	VERBOSE_INFO("NPeriodic EventGraph generation");

	// STEP 1 - Generate Event Graph
	models::EventGraph* eg = algorithms::generateNPeriodicEventGraph(dataflow);
	// STEP 2 - print it

	VERBOSE_INFO("NPeriodic EventGraph generation Done");

	std::cout << eg->printXML();
}

models::EventGraph* algorithms::generateNPeriodicEventGraph (models::Dataflow *  const dataflow) {

	VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");
	VERBOSE_DEBUG_ASSERT(dataflow->is_normalized() == false,"Graph should not be normalized.");


	  {ForEachVertex(dataflow,t) {
	  	  VERBOSE_ASSERT(dataflow->getPhasesQuantity(t) == 1, "Support only SDF");
	  }}


	models::EventGraph * eg = new models::EventGraph();

	/* generate nodes */
	{ForEachVertex(dataflow,pTask) {
		eg->addEventGroup(dataflow->getVertexId(pTask),1,dataflow->getNi(pTask));
	}}


	// DEFINITION DES REENTRANCES
	//******************************************************************

	{ForEachVertex(dataflow,pTask) {
		EXEC_COUNT rfactor = dataflow->getReentrancyFactor(pTask);
		VERBOSE_ASSERT(rfactor <=1 , "unsupported");

		if (rfactor == 1) {
				Vertex source= pTask;
				Vertex target = pTask;

				ARRAY_INDEX source_id = dataflow->getVertexId(source);
				ARRAY_INDEX target_id = dataflow->getVertexId(target);
				ARRAY_INDEX edge_id   = 0;

				EXEC_COUNT Ni = dataflow->getNi(source);
				EXEC_COUNT Nj = dataflow->getNi(target);

				TOKEN_UNIT wp  = 1;
				TOKEN_UNIT vp  = 1;
				TOKEN_UNIT mop = 1;

				TIME_UNIT  tj = dataflow->getVertexDuration(target);


				VERBOSE_DEBUG("Buffer " << edge_id << " from " << dataflow->getVertexName(source) << "to " << dataflow->getVertexName(target) << " : " ) ;

				VERBOSE_DEBUG("  wp  = " << wp  );
				VERBOSE_DEBUG("  vp  = " << vp  );
				VERBOSE_DEBUG("  mop = " << mop );


				for(EXEC_COUNT k = 1; k <= Ni ; k++) {

					long int tmpres = (long int) (std::floor( (double)((double) mop +(double)  wp * ((double) k-1.0)) / (double) vp)) + 1;
					long int bk =  (tmpres % (Nj));
					if (bk == 0) bk = Nj;
					long int ak = ((tmpres - bk) / Nj);

					VERBOSE_ASSERT(bk >= 1  , "bk out of bound");
					VERBOSE_ASSERT(bk <= (long int) Ni , "bk out of bound");
					VERBOSE_ASSERT_EQUALS(ak * (long int) Nj + bk , tmpres);


					models::EventGraphVertex source_event = eg->getEventGraphVertex(source_id,1,k);
					models::EventGraphVertex target_event = eg->getEventGraphVertex(target_id,1,bk);

					TIME_UNIT  d =   tj; // L(a) = l(ti)
					TIME_UNIT  w =   ak;

					eg->addEventConstraint(source_event ,target_event,w,d,edge_id);

				}
		}
	}}


	// DEFINITION DES CONTRAINTES DE PRECEDENCES
	//******************************************************************
	{ForEachChannel(dataflow,c) {

		Vertex source = dataflow->getEdgeSource(c);
		Vertex target = dataflow->getEdgeTarget(c);

		ARRAY_INDEX source_id = dataflow->getVertexId(source);
		ARRAY_INDEX target_id = dataflow->getVertexId(target);
		ARRAY_INDEX edge_id   = dataflow->getEdgeId(c);

		EXEC_COUNT Ni = dataflow->getNi(source);
		EXEC_COUNT Nj = dataflow->getNi(target);

		TOKEN_UNIT wp  = dataflow->getEdgeIn(c);
		TOKEN_UNIT vp  = dataflow->getEdgeOut(c);
		TOKEN_UNIT mop = dataflow->getPreload(c);

		//TIME_UNIT  ti = dataflow->getVertexDuration(source);
		TIME_UNIT  tj = dataflow->getVertexDuration(target);

		VERBOSE_DEBUG("Buffer " << edge_id << " from " << dataflow->getVertexName(source) << "to " << dataflow->getVertexName(target) << " : " ) ;

		VERBOSE_DEBUG("  wp  = " << wp  );
		VERBOSE_DEBUG("  vp  = " << vp  );
		VERBOSE_DEBUG("  mop = " << mop );


		if (wp <= vp ) {
			for(EXEC_COUNT k = 1; k <= Ni ; k++) {

					long int tmpres = (long int) (std::floor( (double)((double) mop +(double)  wp * ((double) k-1.0)) / (double) vp)) + 1;
					long int bk =  (tmpres % (Nj));
					if (bk == 0) bk = Nj;
					long int ak = ((tmpres - bk) / Nj);
					VERBOSE_ASSERT (bk >= 1,"Oups");
					VERBOSE_ASSERT_EQUALS(ak * (long int) Nj + bk , tmpres);


					models::EventGraphVertex source_event = eg->getEventGraphVertex(source_id,1,k);
					models::EventGraphVertex target_event = eg->getEventGraphVertex(target_id,1,bk);

					TIME_UNIT  d =   tj; // L(a) = l(ti)
					TIME_UNIT  w =   ak;

					eg->addEventConstraint(source_event ,target_event,w,d,edge_id);

			}
		} else {
			for(EXEC_COUNT k = 1; k <= Nj ; k++) {
				long int tmpres = (long int) std::ceil((double) (((double)  k * (double)  vp) - (double)  mop) / (double) wp );
				long int ck = 0;
				while ((tmpres - ck * (long int) Ni) <= 0) {
					ck--;
				}
				long int dk =  tmpres - ck * (long int)  Ni;

				VERBOSE_ASSERT_EQUALS(ck * (long int)Ni + dk , tmpres);


				models::EventGraphVertex source_event = eg->getEventGraphVertex(source_id,1,dk);
				models::EventGraphVertex target_event = eg->getEventGraphVertex(target_id,1,k);

				TIME_UNIT  d =   tj; // L(a) = l(ti)
				TIME_UNIT  w =   - ck;

				eg->addEventConstraint(source_event ,target_event,w,d,edge_id);

			}
		}

	}}

	return eg;
}


void algorithms::compute_NPeriodic_throughput (models::Dataflow* const dataflow, parameters_list_t) {

		// STEP 0.1 - PRE
		VERBOSE_ASSERT(dataflow,TXT_NEVER_HAPPEND);
		VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");

		//STEP 1 - Generate Event Graph
		VERBOSE_INFO("N-Periodic EventGraph generation");
		models::EventGraph* eg = generateNPeriodicEventGraph(dataflow);
		VERBOSE_INFO("NPeriodic EventGraph generation Done, edges = " << eg->getConstraintsCount() << " vertex = " << eg->getEventCount());
		//STEP 2 - resolve the MCRP on this Event Graph
		std::pair<TIME_UNIT,std::vector<models::EventGraphEdge> > howard_res = eg->MinCycleRatio();
		VERBOSE_DEBUG("Critical circuit is about " << howard_res.second.size() << " edges.");

		TIME_UNIT frequency = howard_res.first;

		if (VERBOSE_IS_DEBUG()) {
	    		std::cout << (eg->printDOT());
	    		VERBOSE_INFO( "Maximum throughput is " << frequency );
	    	} else {
	    		std::cout << "Maximum throughput is " << frequency  << std::endl;
	    	}
}


void algorithms::compute_NCleanPeriodic_throughput (models::Dataflow* const dataflow, parameters_list_t){

		// STEP 0.1 - PRE
		VERBOSE_ASSERT(dataflow,TXT_NEVER_HAPPEND);
		VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");

		//STEP 1 - Generate Event Graph
		VERBOSE_INFO("N-Periodic EventGraph generation");
		models::EventGraph* eg = generateNPeriodicEventGraph(dataflow);
		VERBOSE_INFO("NPeriodic EventGraph generation Done, edges = " << eg->getConstraintsCount() << " vertex = " << eg->getEventCount());
		eg->FullConnectionned();
		VERBOSE_INFO("NPeriodic Reducted EventGraph generation Done, edges = " << eg->getConstraintsCount() << " vertex = " << eg->getEventCount());

		//STEP 2 - resolve the MCRP on this Event Graph
		std::pair<TIME_UNIT,std::vector<models::EventGraphEdge> > howard_res = eg->MinCycleRatio();
		VERBOSE_DEBUG("Critical circuit is about " << howard_res.second.size() << " edges.");

		TIME_UNIT frequency = howard_res.first;

		if (VERBOSE_IS_DEBUG()) {
	    		std::cout << (eg->printDOT());
	    		VERBOSE_INFO( "Maximum throughput is " << frequency );
	    	} else {
	    		std::cout << "Maximum throughput is " << frequency  << std::endl;
	    	}
}


void algorithms::compute_1Periodic_memory (models::Dataflow* const dataflow, parameters_list_t params) {
    std::map<Vertex,EXEC_COUNT> kvector;
    {ForEachVertex(dataflow,t) {
        VERBOSE_ASSERT(dataflow->getPhasesQuantity(t) == 1,"This graph is not an SDF.");
        kvector[t] = 1;
    }}
    KPeriodic_memory(dataflow,kvector,params);
}

void algorithms::compute_NPeriodic_memory   (models::Dataflow* const  dataflow, parameters_list_t params) {
    std::map<Vertex,EXEC_COUNT> kvector;
    {ForEachVertex(dataflow,t) {
        VERBOSE_ASSERT(dataflow->getPhasesQuantity(t) == 1,"This graph is not an SDF.");
        kvector[t] = dataflow->getNi(t);
    }}
    KPeriodic_memory(dataflow,kvector,params);
}

void algorithms::compute_KPeriodic_memory (models::Dataflow* const dataflow, parameters_list_t params) {
    std::map<Vertex,EXEC_COUNT> kvector = get_Kvector(dataflow);

    KPeriodic_memory(dataflow,kvector,params);
}


void algorithms::KPeriodic_memory   (models::Dataflow* const  dataflow,  std::map<Vertex,EXEC_COUNT>& kvector, parameters_list_t params) {

    commons::ValueKind CONTINUE_OR_INTEGER = commons::KIND_CONTINUE;

    VERBOSE_INFO("Please note you can specify the INTEGERSOLVING, ILPGENERATIONONLY parameters.");
    if (params.find("INTEGERSOLVING")!= params.end() ) CONTINUE_OR_INTEGER = commons::KIND_INTEGER;

    VERBOSE_ASSERT(dataflow,TXT_NEVER_HAPPEND);


    VERBOSE_INFO("Getting period ...");


    // STEP 0 - CSDF Graph should be normalized
    VERBOSE_ASSERT(computeRepetitionVector(dataflow),"inconsistent graph");
    // STEP 1 - Compute normalized period
    TIME_UNIT PERIOD = 0 ;
	if (params.find("PERIOD")!= params.end() ) PERIOD =  commons::fromString<TIME_UNIT>(params["PERIOD"]);
	VERBOSE_ASSERT (PERIOD > 0, "The PERIOD must be defined");
    TIME_UNIT FREQUENCY = 1.0 / PERIOD;


    VERBOSE_INFO("Generate Graph ...");

    //##################################################################
    // Linear program generation
    //##################################################################
    const std::string problemName =  "NPeriodicSizing_" + dataflow->getGraphName() + "_" + commons::toString(FREQUENCY) + "_" + ((CONTINUE_OR_INTEGER == commons::KIND_INTEGER) ? "INT" : "");
    commons::GLPSol g = commons::GLPSol(problemName,commons::MIN_OBJ);

    // Starting times
    //******************************************************************
    {ForEachVertex(dataflow,t) {
        std::string name = dataflow->getVertexName(t);
        VERBOSE_ASSERT(name != "", "Unnamed task is unsupported");
        for(EXEC_COUNT k = 1; k <= kvector[t] ; k++) {
            g.addColumn("s_" + commons::toString<EXEC_COUNT>(k) + "_" + name,commons::KIND_CONTINUE,commons::bound_s(commons::LOW_BOUND,0),0);
        }
    }}


    // Constraints
    //******************************************************************

    {ForEachEdge(dataflow,c) {
        const Vertex source   = dataflow->getEdgeSource(c);
        const Vertex target   = dataflow->getEdgeTarget(c);

        const std::string  buffername= dataflow->getEdgeName(c);
        VERBOSE_ASSERT(buffername != "", "Unnamed edge is unsupported");
        const std::string  feedbackbuffername= "feedback_" + dataflow->getEdgeName(c);
        const std::string  feedback_mo_name   = "Mop_" + feedbackbuffername;
        const std::string  mo_name   = "Mop_" + buffername;
        const std::string  sourceStr = dataflow->getVertexName(source);
        const std::string  targetStr = dataflow->getVertexName(target);
        const EXEC_COUNT  Ni        =  dataflow->getNi(source);
        const EXEC_COUNT  Nj        =  dataflow->getNi(target);
        const TIME_UNIT  mu_i        =  ((TIME_UNIT) kvector[source] * (TIME_UNIT) (PERIOD)) / (TIME_UNIT) Ni;
        const TIME_UNIT  mu_j        = ((TIME_UNIT) kvector[target] * (TIME_UNIT) (PERIOD)) / (TIME_UNIT) Nj;

        const TOKEN_UNIT  in_b        = dataflow->getEdgeIn(c);
        const TOKEN_UNIT  ou_b        = dataflow->getEdgeOut(c);

        const TOKEN_UNIT  gcdb      = std::gcd((in_b),(ou_b));
        const TOKEN_UNIT  gcdk      = std::gcd( kvector[source]  * (in_b), kvector[target] * (ou_b));

        const TOKEN_UNIT  mop      =  commons::floor(dataflow->getPreload(c),gcdb);


        VERBOSE_DEBUG("Mu_i = " << mu_i);
        VERBOSE_DEBUG("Mu_j = " << mu_j);


        // Feedback Buffer marking (in objectif, its Theta pondered)
        g.addColumn("loopbackfix",CONTINUE_OR_INTEGER,commons::bound_s(commons::FIX_BOUND,dataflow->getVerticesCount()),2);
        g.addColumn(mo_name,CONTINUE_OR_INTEGER,commons::bound_s(commons::FIX_BOUND,dataflow->getPreload(c)),(double) dataflow->getTokenSize(c));
        g.addColumn(feedback_mo_name,CONTINUE_OR_INTEGER,commons::bound_s(commons::LOW_BOUND,0),(double) dataflow->getTokenSize(c));

        const TIME_UNIT       ltai    = dataflow->getVertexDuration(source,1);
        const TIME_UNIT       ltaj    = dataflow->getVertexDuration(target,1);
        const TOKEN_UNIT  Ha        =   std::max((TOKEN_UNIT)0, in_b - ou_b);

        for(EXEC_COUNT ai = 1; ai <= kvector[source] ; ai++) {
            int saicolid = g.getColumn("s_" + commons::toString<EXEC_COUNT>(ai) + "_"+ sourceStr );
            for(EXEC_COUNT  aj = 1; aj <= kvector[target] ; aj++) {
                int sajcolid = g.getColumn("s_" + commons::toString<EXEC_COUNT>(aj) + "_"+ targetStr );


                // *** Normal Buffer constraint computation
                const TOKEN_UNIT  alphamin  =   commons::ceil(Ha + (TOKEN_UNIT) aj * ou_b - (TOKEN_UNIT) ai * in_b - mop,gcdk);
                const TOKEN_UNIT  alphamax  =   commons::floor(  in_b + (TOKEN_UNIT)aj * ou_b - (TOKEN_UNIT)ai * in_b  - mop - 1 ,gcdk);


                if (alphamin <= alphamax) { // check if contraint exist
                    const std::string pred_row_name = "precedence_" + buffername + "_" + commons::toString<EXEC_COUNT>(ai) + "_" + commons::toString<EXEC_COUNT>(aj);
                    TIME_UNIT coef = ltai + (TIME_UNIT) alphamax * (TIME_UNIT) ( mu_i /  ((TIME_UNIT) in_b* (double) kvector[source]));
                    //VERBOSE_DEBUG("LP : s_"  <<  commons::toString<EXEC_COUNT>(aj) <<  "_" <<  targetStr  << " - " << "s_"  <<  commons::toString<EXEC_COUNT>(ai) <<  "_" <<  sourceStr  << " >= " << ltai << " + " << (TIME_UNIT) alphamax << " * (" << mu_i << "/" << in_b <<  ")");
                    //VERBOSE_DEBUG("     s_"  <<  commons::toString<EXEC_COUNT>(aj) <<  "_" <<  targetStr  << " - " << "s_"  <<  commons::toString<EXEC_COUNT>(ai) <<  "_" <<  sourceStr  << " >= " << coef);

                    int rowid = g.addRow(pred_row_name,commons::bound_s(commons::LOW_BOUND, (double) coef));


                    if ( (ai != aj) || (source != target)) {
                        g.fastAddCoef(rowid ,sajcolid    ,  1        );
                        g.fastAddCoef(rowid ,saicolid    , -1        );
                    }
                }

                // *** Feedback Buffer constraint computation
                const std::string pred_row_name = "precedence_" + feedbackbuffername + "_" +
                        commons::toString<EXEC_COUNT>(aj) + "_" + commons::toString<EXEC_COUNT>(ai);
                const std::string local_mo_name = feedback_mo_name  + "_" + commons::toString<EXEC_COUNT>(ai) + "_" + commons::toString<EXEC_COUNT>(aj) ;

                //TOKEN_UNIT      quot = commons::floor(- crajm1 + cwai  - gcdz,gcdz);
                //TOKEN_UNIT      rem =  TOKEN_UNIT - quot;
                //TIME_UNIT       coef     =  ltaj  + (TIME_UNIT) ( mu_j /  (TIME_UNIT) Zj) * quot;
                TIME_UNIT       coef     =  ltaj ;
                //VERBOSE_DEBUG(" add feedback coef, " << ltaj << " + " << (TIME_UNIT)  (quot - gcdz) << " * " << mu_j << "=" << coef);

                int localmopcol = g.addColumn(local_mo_name,CONTINUE_OR_INTEGER,commons::bound_s(),0);
                // main constraint
                int rowid = g.addRow(pred_row_name  ,commons::bound_s(commons::LOW_BOUND,coef));
                g.fastAddCoef(rowid ,localmopcol , -   (double) ( (double) mu_j /  ((double) ou_b * (double) kvector[target])) * (double) gcdk );

                    if ( (ai != aj) || (source != target)) {
                        g.fastAddCoef(rowid ,saicolid   ,  1        );
                        g.fastAddCoef(rowid ,sajcolid    , -1        );
                    }
                    //VERBOSE_DEBUG("LP : s_"  <<  commons::toString<EXEC_COUNT>(ai) <<  "_" <<  sourceStr  << " - " << "s_"  <<  commons::toString<EXEC_COUNT>(aj) <<  "_" <<  targetStr  << " >= " << ltaj << " + f * " << gcdk <<  " *(" << mu_j << "/" << ou_b <<  ")");
                    //VERBOSE_DEBUG("     s_"  <<  commons::toString<EXEC_COUNT>(ai) <<  "_" <<  sourceStr  << " - " << "s_"  <<  commons::toString<EXEC_COUNT>(aj) <<  "_" <<  targetStr  << " - f * " << (TIME_UNIT) ( mu_j /  (TIME_UNIT) ou_b) * gcdk  << " >= " << coef);


                    //local mop bound
                    // -Mo -WaPred + Ra - 1 - gcdz + 1 \leq local_mo * gcdz  \leq -Mo -WaPred + Ra - 1
                    // Low bound :
                    //   -Mo -WaPred + Ra - 1 - gcdz + 1 \leq local_mo
                    //   -Mo -gcdz * local_mo  \leq WaPred - Ra + gcdz
                    //   Ra - WaPred - gcdz - gcdz * local_mo  \leq + Mo
                    //   Mo \geq  Ra - WaPred - gcdz - gcdz * local_mo
                    g.addRow (local_mo_name + "_L",commons::bound_s(commons::LOW_BOUND, ou_b - (TOKEN_UNIT) aj * ou_b  + (TOKEN_UNIT)ai * in_b  - gcdk ));
                    g.addCoef(local_mo_name + "_L",local_mo_name        , (double) gcdk   );
                    g.addCoef(local_mo_name + "_L",feedback_mo_name     , 1       );

                    //VERBOSE_DEBUG("LP : s_"  << feedback_mo_name << "+" << local_mo_name  << " * " << gcdk << " >= " << ou_b << " - " << aj << " * " << ou_b  << " + " << ai << " * " << in_b  << " - " << gcdk  <<  ")");
                    //VERBOSE_DEBUG("[Mo] s_"  << feedback_mo_name << "+" << local_mo_name  << " * " << gcdk << " >= " << ou_b - (TOKEN_UNIT)aj * ou_b  + (TOKEN_UNIT)ai * in_b  - gcdk  <<  ")");


                    // resume
                    //
                    // -local_mo * gcdz - Mo \leq  WaPred - Ra + gcdz
                    //  local_mo * gcdz + Mo \leq -WaPred + Ra - 1

            }
        }

    }}


    // LoopBack
    //******************************************************************

    {ForEachVertex(dataflow,t) {
		const TIME_UNIT timefactor    = (dataflow->getReentrancyFactor(t) <= 0)?0:1;

        const std::string  name   = dataflow->getVertexName(t);
        const EXEC_COUNT max_k    = kvector[t] ;
        const TIME_UNIT  mu_i     = ((TIME_UNIT) max_k * (TIME_UNIT) (PERIOD)) / (TIME_UNIT) dataflow->getNi(t);
        VERBOSE_DEBUG( "Mu_" << name << " = " << mu_i  << " lti = " << commons::toString( dataflow->getVertexPhaseDuration(t)) << "Ni = " << dataflow->getNi(t) << "Phi=" << dataflow->getPhasesQuantity(t));

        // constraintes k --> k + 1

        for(EXEC_COUNT k = 1; k < max_k ; k++) {
            EXEC_COUNT kp = k + 1;
            const std::string pred_row_name = "reentrancy_" + name + "_" + commons::toString<EXEC_COUNT>(k) + "_" + commons::toString<EXEC_COUNT>(kp);
            const TIME_UNIT       ltk    = dataflow->getVertexDuration(t,1) * timefactor;

            g.addRow(pred_row_name,commons::bound_s(commons::LOW_BOUND,(double)ltk));
            g.addCoef(pred_row_name ,"s_" + commons::toString<EXEC_COUNT>(kp) + "_"+ name    ,  1      );
            g.addCoef(pred_row_name ,"s_" + commons::toString<EXEC_COUNT>(k) + "_"+ name    , -1       );
        }

        // constraintes last_k --> 1
        const std::string pred_row_name = "reentrancy_" + name + "_phi_1";
        const TIME_UNIT       ltk    = dataflow->getVertexDuration(t,1) * timefactor;
        const TIME_UNIT coef = ltk -  mu_i;

        VERBOSE_DEBUG("LoopBack for "<< name << " is " << ltk  <<  "-" << mu_i << " = " << coef);

        g.addRow(pred_row_name,commons::bound_s(commons::LOW_BOUND,(double)coef));

        if (max_k > 1){
            g.addCoef(pred_row_name ,"s_1_"+ name    ,  1      );
            g.addCoef(pred_row_name ,"s_" + commons::toString<EXEC_COUNT>(max_k) + "_"+ name    , -1       );
        }


    }}

    //##################################################################
    // SOLVE LP
    //##################################################################

    // commons::GLPParameters ilp_params = commons::getDefaultParams();

    // ilp_params.general_doScale = true;
    // ilp_params.linear_doAdvBasis = true;
    // ilp_params.linear_method = commons::DUAL_LINEAR_METHOD;
    //
    // bool sol = g.solve(ilp_params);


    VERBOSE_INFO("Solving problem ...");

    if (params.find("ILPGENERATIONONLY")!= params.end() )  {
        g.writeMPSProblem();
        return;
    }
    bool sol = g.solveWith();

    VERBOSE_INFO("Solved, gathering results ...");

    //##################################################################
    // GATHERING RESULTS
    //##################################################################

    // BUFFER SIZES
    //******************************************************************
    if (sol) {
        DATA_UNIT total_buffer_size = 0;
        // ** Good value retreiving method **
        ARRAY_INDEX  edge_indice = 0; // FIXME
        {ForEachEdge(dataflow,c) {
            edge_indice++;
            const Vertex source   = dataflow->getEdgeSource(c);
            const Vertex target   = dataflow->getEdgeTarget(c);

            const std::string  buffername= dataflow->getEdgeName(c);
            const std::string  feedbackbuffername= "feedback_" + dataflow->getEdgeName(c);
            const std::string  feedback_mo_name   = "Mop_" + feedbackbuffername;
            const std::string  sourceStr = dataflow->getVertexName(source);
            const std::string  targetStr = dataflow->getVertexName(target);

            const TOKEN_UNIT  in_b        = dataflow->getEdgeIn(c);
            const TOKEN_UNIT  ou_b        = dataflow->getEdgeOut(c);
            const TOKEN_UNIT  gcdk      = std::gcd( kvector[source]  * (in_b), kvector[target] * (ou_b));


            TOKEN_UNIT feedbackmopmax =  commons::ceil(g.getValue(feedback_mo_name),1);
            if (CONTINUE_OR_INTEGER == commons::KIND_INTEGER) feedbackmopmax = (TOKEN_UNIT) round(g.getIntegerValue(feedback_mo_name));
            VERBOSE_DEBUG(dataflow->getEdgeName(c) << " : Starting with " << g.getValue(feedback_mo_name) << " => " << feedbackmopmax );
            for(EXEC_COUNT ai = 1; ai <= kvector[source] ; ai++) {

                for(EXEC_COUNT  aj = 1; aj <= kvector[target] ; aj++) {


                    //   -Mo -WaPred + Ra - 1 - gcdz + 1 \leq local_mo
                    //   -Mo -gcdz * local_mo  \leq WaPred - Ra + gcdz
                    //   -Mo \leq WaPred - Ra + gcdz  + gcdz * local_mo
                    //   Mo   >= -  WaPred + Ra - gcdz - gcdz * local_mo


                    const std::string pred_row_name = "precedence_" + feedbackbuffername + "_" + commons::toString<EXEC_COUNT>(aj) + "_" + commons::toString<EXEC_COUNT>(ai);
                    const std::string local_coef = feedback_mo_name  + "_" + commons::toString<EXEC_COUNT>(ai) + "_" + commons::toString<EXEC_COUNT>(aj) ;

                    if (!g.haveValue(local_coef)) {
                        VERBOSE_FAILURE();
                    }

                    TOKEN_UNIT from_lp = commons::floor(g.getValue(local_coef),1);
                    if (CONTINUE_OR_INTEGER == commons::KIND_INTEGER)
                        from_lp =  (TOKEN_UNIT) round(g.getIntegerValue(local_coef));

                    // *** Feedback Buffer constraint computation
                    TOKEN_UNIT bound =  - from_lp * gcdk  + ou_b - aj * ou_b + ai * in_b - gcdk;
                    if (bound > feedbackmopmax) {
                        VERBOSE_DEBUG("   -> new fmax => " << g.getValue(local_coef) << "=> " << from_lp);
                        VERBOSE_DEBUG("   -> new bound = " <<  - from_lp <<  "*"  << gcdk << "-" <<  - ou_b + aj * ou_b << "+" <<ai * in_b << "-" << gcdk<< "=" << bound );

                        feedbackmopmax = bound;
                    }
                }
            }

            TOKEN_UNIT buffersize =  feedbackmopmax  + dataflow->getPreload(c);
            VERBOSE_INFO(dataflow->getEdgeName(c) << " :" << g.getValue(feedback_mo_name) << " + " << dataflow->getPreload(c) << " -> " << feedbackmopmax << " + " << dataflow->getPreload(c)<< " = " << buffersize  );
            //TOKEN_UNIT from_integer_mop          = commons::floor(g.getIntegerValue(feedback_mo_name),1);

            total_buffer_size += buffersize * dataflow->getTokenSize(c);
        }}

        VERBOSE_INFO("Loopback buffers : " << dataflow->getVerticesCount());
        std::cout << "Total buffer size : " << total_buffer_size
                << " + 2 * " << dataflow->getVerticesCount() << " = "
                << total_buffer_size + 2 * dataflow->getVerticesCount() << std::endl ;
    } else {
        VERBOSE_ERROR("No feasible solution");
    }

    VERBOSE_INFO("Done");
    return;

}
