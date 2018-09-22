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
	unsigned long long maxMemory;
	unsigned long long memFree;
	unsigned long long inflated; //whatever was given to hypervisor
}memStats;

typedef struct memStats * memStatsPtr;



static memStats getMemStats(virDomainPtr domain){
	virDomainSetMemoryStatsPeriod(domain, 1, VIR_DOMAIN_AFFECT_CURRENT);
	memStats m;
	m.domain = domain;
	//m.maxMemory = virDomainGetMaxMemory(domain);

	m.inflated = 0;
	int nr_stats = VIR_DOMAIN_MEMORY_STAT_NR;
	virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct)*nr_stats);
	virDomainMemoryStats(domain, stats, nr_stats, 0);
	for(int i=0; i<nr_stats; i++){
		if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_UNUSED){
			m.memFree = stats[i].val;
		}
		if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
			m.maxMemory = stats[i].val;
		}
	}
	return m;
}

static double getFreeMemPercentage(virConnectPtr conn){
	//check whether free memory available to host is less than 5%
	int nparams=4;
	unsigned long long total, used;
	
	virNodeMemoryStatsPtr params = malloc(sizeof(virNodeMemoryStats) * nparams);
	memset(params, 0, sizeof(virNodeMemoryStats) * nparams);
	virNodeGetMemoryStats(conn,VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0);
		
	for(int i=0; i < nparams; i++){
		if(strcmp(params[i].field,VIR_NODE_MEMORY_STATS_TOTAL)==0){
			total = params[i].value;
		} else if(strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE)==0){
			used = params[i].value;
		}
	}
	free(params);
	return 100*(total-used)/total;
}

static void updateMemStats(virDomainPtr domain, memStatsPtr memStat){
	virDomainSetMemoryStatsPeriod(domain, 1, VIR_DOMAIN_AFFECT_CURRENT);
	int nr_stats = VIR_DOMAIN_MEMORY_STAT_NR;
	virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct)*nr_stats);
	virDomainMemoryStats(domain, stats, nr_stats, 0);
	for(int i=0; i<nr_stats; i++){
		if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_UNUSED){
			memStat->memFree = stats[i].val;
		}
		if(stats[i].tag==VIR_DOMAIN_MEMORY_STAT_AVAILABLE){
			memStat->maxMemory = stats[i].val;
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
		memDomains[i] = getMemStats(activeDomains[i]);			
		printf("domain=%d,maxMemory=%lld, inflated=%lld, memFree=%lld\n", i,memDomains[i].maxMemory, memDomains[i].inflated, memDomains[i].memFree);fflush(stdout);
	}			
	
	int LOADED_THRESHOLD = 15;
	int FREE_THRESHOLD = 30;

	while(1){
		
		double percentFree;		
	
		for(int i=0; i<numDomains; i++){
			updateMemStats(activeDomains[i], &memDomains[i]);			
printf("domain=%d,maxMemory=%lld, inflated=%lld, memFree=%lld\n", i,memDomains[i].maxMemory, memDomains[i].inflated, memDomains[i].memFree);fflush(stdout);
		}	
			
		//find the domain which has least free memory and domain which has max
		int domMax=-1, domMin=-1;
		unsigned long long maxFree=0, minFree=INT_MAX;
		for(int i=0; i<numDomains; i++){
			if(memDomains[i].memFree > maxFree){
				maxFree = memDomains[i].memFree;
				domMax = i;
			}
			if(memDomains[i].memFree < minFree){
				minFree = memDomains[i].memFree;
				domMin = i;
			}
		}
		printf("Domain with most free=%d\n", domMax);fflush(stdout);
		printf("Domain with least free=%d\n", domMin);fflush(stdout);
		
		double maxPer = 100*(memDomains[domMax].memFree)/memDomains[domMax].maxMemory;
		double minPer = 100*(memDomains[domMin].memFree)/memDomains[domMin].maxMemory;

		printf("Domain with most free percent=%lf\n", maxPer);fflush(stdout);
		printf("Domain with least free percent=%lf\n", minPer);fflush(stdout);	
		
		//if some domain needs memory
		if(maxPer >= FREE_THRESHOLD && minPer <= LOADED_THRESHOLD){
			printf("maxPer > 5 and minPer < 5\n");fflush(stdout);	
			//take away 50% of free memory from one and give to other one

			printf("D%d  new memory=%lld\n", domMax, (memDomains[domMax].maxMemory - 50*(memDomains[domMax].maxMemory)/100));fflush(stdout);
			virDomainSetMemory(memDomains[domMax].domain,  (memDomains[domMax].maxMemory - (50*memDomains[domMax].maxMemory)/100)/1024);
			
			printf("D%d new memory=%lld\n", domMin, memDomains[domMin].memFree + (50*memDomains[domMax].maxMemory/100));fflush(stdout);
			virDomainSetMemory(memDomains[domMin].domain,  (memDomains[domMin].maxMemory + (50*memDomains[domMax].maxMemory)/100)/1024);
		} else if(minPer <= LOADED_THRESHOLD) { 
			printf("minPer < 5\n");fflush(stdout);	
			//assign 5% more to domain with least free memory and let hypervisor worry about it
			printf("D%d new memory=%lld\n", domMin, memDomains[domMin].maxMemory + (FREE_THRESHOLD*memDomains[domMin].maxMemory/100));fflush(stdout);
			virDomainSetMemory(memDomains[domMin].domain,(memDomains[domMin].maxMemory + (FREE_THRESHOLD*memDomains[domMin].maxMemory/100))/1024);
		} else if(maxPer >= FREE_THRESHOLD){
			//take away 50% of memory 
			printf("maxPer > 5\n");fflush(stdout);
			printf("D%d new memory=%lld\n", domMax,  memDomains[domMax].maxMemory - (50*memDomains[domMax].maxMemory/100));fflush(stdout);
			virDomainSetMemory(memDomains[domMax].domain, (memDomains[domMax].maxMemory - (50*memDomains[domMax].maxMemory/100))/1024);
		}
		
		//if host needs more memory, go through all domains, take threshold% away from them
				
		percentFree = getFreeMemPercentage(conn);
		printf("free in host=%lf\n", percentFree);fflush(stdout);
	
		if(percentFree <= LOADED_THRESHOLD){
			for(int i=0; i<numDomains; i++){
				virDomainSetMemory(memDomains[i].domain, (memDomains[i].maxMemory - (LOADED_THRESHOLD*memDomains[i].maxMemory/100))/1024);
			}
		}
		sleep(timeInterval);
		
	}
	for(int i=0; i<numDomains; i++){
		virDomainFree(activeDomains[i]);
	}
	free(activeDomains);
}
