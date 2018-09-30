#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<libvirt/libvirt.h>
#include<libvirt/virterror.h>
#include<unistd.h>
#include<math.h>
#include<limits.h>


typedef struct memStats{
	virDomainPtr domain;
	unsigned long unused; //how much unused memory is present
	unsigned long available;//how much memory is available for usage
}memStats;

typedef struct memStats * memStatsPtr;


static unsigned long getFreeMemInHost(virConnectPtr conn){
	int nparams=4;
	unsigned long freeMem;
	
	virNodeMemoryStatsPtr params = malloc(sizeof(virNodeMemoryStats) * nparams);
	memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
	virNodeGetMemoryStats(conn,VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0);
		
	for(int i=0; i < nparams; i++){
		 if(strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE)==0){
			freeMem = params[i].value;break;
		}
	}
	free(params);
	return freeMem;
}

static void updateMemStats(virDomainPtr domain, memStatsPtr memStat){
	memStat->domain = domain;
	
	int nr_stats = VIR_DOMAIN_MEMORY_STAT_NR;
	virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct)*nr_stats);
	virDomainMemoryStats(domain, stats, nr_stats, 0);
	for(int i=0; i<nr_stats; i++){
		if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_UNUSED){
			memStat->unused = stats[i].val;
		}
		if(stats[i].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
			memStat->available = stats[i].val;
		}

	}

}

int main(int argc, char* argv[]){
	//connecting to the hypervisor
	virConnectPtr conn = virConnectOpen("qemu:///system");
	if(!conn){
		printf("unable to connect to hypervisor");
		return 1;
	} 

	//extract the time interval
	int timeInterval = strtol(argv[1], NULL, 0);
	virDomainPtr * activeDomains = NULL;
	int numDomains = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if(numDomains==-1){
		printf("unable to get list of domains");
		return -1;
	}

	memStatsPtr memDomains = malloc(sizeof(memStats)*numDomains);	
	for(int i=0; i<numDomains; i++){
		virDomainSetMemoryStatsPeriod(activeDomains[i], 1, VIR_DOMAIN_AFFECT_CURRENT);			
	}			
	
	int LOADED_THRESHOLD = 150 * 1024;
	int FREE_THRESHOLD = 200 * 1024;


	while(1){		
	
		for(int i=0; i<numDomains; i++){
			updateMemStats(activeDomains[i], &memDomains[i]);			
		}	

		//find the domains with most and least amount of unused memory
		int most=0, least=0;
		unsigned long mostMem =memDomains[0].unused, leastMem=memDomains[0].unused;
		for(int i=1; i<numDomains; i++){
			if(memDomains[i].unused > mostMem){
				most = i;
				mostMem = memDomains[i].unused;
			}
			if(memDomains[i].unused < leastMem){
				leastMem = memDomains[i].unused;
				least = i;
			}
		}	

		//if there is a domain which has very less unused memory
		if(memDomains[least].unused <= LOADED_THRESHOLD){
			//if the domain with most free memory can afford to give memory, take memory away
			if(memDomains[most].unused >= FREE_THRESHOLD){
				//balloon can be inflated 
				virDomainSetMemory(memDomains[most].domain, memDomains[most].available-LOADED_THRESHOLD);
				virDomainSetMemory(memDomains[least].domain, memDomains[least].available+LOADED_THRESHOLD);
			} else { //give the memory from host
				virDomainSetMemory(memDomains[least].domain, memDomains[least].available+FREE_THRESHOLD);
			}
		} else if(memDomains[most].unused >= FREE_THRESHOLD){ //there is a domain which is using unnecessary memory
			virDomainSetMemory(memDomains[most].domain, memDomains[most].available-LOADED_THRESHOLD);
		}

		unsigned long memFreeInHost = getFreeMemInHost(conn);
		if(memFreeInHost <=LOADED_THRESHOLD){
			//go through all domains and take away memory wherever possible
			for(int i=0; i< numDomains; i++){
				if(memDomains[i].unused >= FREE_THRESHOLD){
					virDomainSetMemory(memDomains[i].domain, memDomains[i].available-LOADED_THRESHOLD);
				}
			}
		}

		sleep(timeInterval);
		
	}
	for(int i=0; i<numDomains; i++){
		virDomainFree(activeDomains[i]);
	}
	free(activeDomains);
	virConnectClose(conn);
	return 0;
}
