#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<libvirt/libvirt.h>
#include<libvirt/virterror.h>
#include<unistd.h>
#include<math.h>
#include<limits.h>


typedef struct domainStats {
	virDomainPtr domain;
	unsigned long long time;
	double usage;
} domainStats;

typedef domainStats * domainStatsPtr;

typedef struct pcpuStats {
	unsigned long long time;
	double usage;
} pcpuStats;

typedef pcpuStats * pcpuStatsPtr;

static int calculateDomainUsage(domainStatsPtr currDomainStats, domainStatsPtr prevDomainStats, int t, int numDomains){
	
	for(int i=0; i<numDomains; i++){
		currDomainStats[i].usage = 100*(currDomainStats[i].time - prevDomainStats[i].time)/(t*pow(10,9));
	}
	
	return 0;
}

static int calculatePcpuUsage(pcpuStatsPtr currPcpuStats, pcpuStatsPtr prevPcpuStats, int numPcpus, int t){
	
	for(int i=0; i<numPcpus; i++){ //0 corresponds to cpu0, 1 to cpu1 and so on
		currPcpuStats[i].usage = 100*(currPcpuStats[i].time - prevPcpuStats[i].time)/(t*pow(10, 9));
		printf("cpu=%d, curr=%lld, prev=%lld\n", i, currPcpuStats[i].time, prevPcpuStats[i].time);
	}
	
	return 0;
}

/*static pcpuStats getPcpuUsage(virConnectPtr conn, int cpuNum){
	
	pcpuStats p ;
	int nparams = 0;
	virNodeCPUStatsPtr params;
	if (virNodeGetCPUStats(conn, cpuNum, NULL, &nparams, 0) == 0 && nparams != 0) {
		params = malloc(sizeof(virNodeCPUStats) * nparams);
		memset(params, 0, sizeof(virNodeCPUStats) * nparams);
		if(virNodeGetCPUStats(conn, cpuNum, params, &nparams, 0)==-1){
			printf("Unable to get node cpu stats");
		} 
	}
	for(int i=0; i<nparams; i++){
		if(strcmp(params[i].field, VIR_NODE_CPU_STATS_KERNEL)==0 ||
			strcmp(params[i].field, VIR_NODE_CPU_STATS_USER)==0 ||
			strcmp(params[i].field, VIR_NODE_CPU_STATS_IOWAIT)==0
			){
			p.time += params[i].value;	
		}
	}
	
	p.usage=0.0;
	return p;

}*/
static void getPcpuUsage(domainStatsPtr currDomainStats,pcpuStatsPtr currPcpuStats, int numDomains){
	for(int i=0; i<numDomains; i++){
		virVcpuInfoPtr info = malloc(sizeof(virVcpuInfo));
		virDomainGetVcpus(currDomainStats[i].domain, info, 1, NULL, 0);
		currPcpuStats[info->cpu].time += currDomainStats[i].time; 
		printf("time for pcpu%d is %lld\n", info->cpu, currPcpuStats[info->cpu].time);
	}
}

static domainStats getCurrVcpuTime(virDomainStatsRecordPtr dStats){
	domainStats d;
	d.domain = dStats->dom;
	
	//extract params variable
	virTypedParameterPtr params = dStats->params;
	int nparams = dStats->nparams;

	//get the cpu_time
	//Assumption is that there is only one vcpu
	unsigned long long cpuTime;
	for(int i=0; i<nparams; i++){
		if(strlen(params[i].field)>4){
			char *last = &params[i].field[strlen(params[i].field)-4]; //extracting out the last four characters to check if it is time
			if(strcmp(last, "time")==0){
				cpuTime = params[i].value.ul;
				break;
			}
		}
	}
	d.time = cpuTime;
	d.usage = 0.0;
	return d;
}

static void pinVcpuToPcpu(domainStatsPtr currDomainStats, pcpuStatsPtr currPcpuStats, int numDomains, int numPcpus){
	double usage = currPcpuStats[0].usage;
	printf("cpu=%d, usage=%f\n", 0, currPcpuStats[0].usage);
	int freestPcpu = 0;int stop=1;
	for(int i=1; i<numPcpus; i++){
		printf("cpu=%d, usage=%f\n", i, currPcpuStats[i].usage);
		//checking if there is any pcpu which is a lot freer than rest
		double diff = currPcpuStats[i].usage > usage? currPcpuStats[i].usage - usage: usage - currPcpuStats[i].usage;
		if(diff > 10.0){
			stop =0;
		}
			
		if(currPcpuStats[i].usage < usage){
			freestPcpu = i;
			usage = currPcpuStats[i].usage;
		}
	}

	if(stop==1) {
		printf("The pcpus are balanced");
		return;
	}	

	//find the domain with heaviest load
	int busiestDomain = -1;
	usage = INT_MIN;
	for(int i=0; i<numDomains; i++){
		if(currDomainStats[i].usage > usage){
			busiestDomain = i;
			usage = currDomainStats[i].usage;
		}
	}
	if(busiestDomain ==-1) return;
	
	
	//find pcpu with lightest load
	

	unsigned char cpuMap = 0x1 << freestPcpu;
	printf("cpuMap=%d\n", cpuMap);
	
	//printf("%d, free=%d\n", cpuMap, freestPcpu);fflush(stdout);
	
	if(virDomainPinVcpu(currDomainStats[busiestDomain].domain, 0, &cpuMap, (numPcpus/8)+1)==-1){
		printf("Unable to pin vcpu to pcpu");
	}
	
	virDomainGetVcpuPinInfo(currDomainStats[busiestDomain].domain, 1, &cpuMap, (numPcpus/8)+1, 0);
	printf("After changing%d\n", cpuMap);
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
	
	domainStatsPtr prevDomainStats = NULL;
	pcpuStatsPtr prevPcpuStats = NULL;
	int prevNumDomains=0;
	int prevNumPcpu = 0;

	while(1){

		//get stats for all the domains
		virDomainStatsRecordPtr *domainStatsList = NULL;	
		unsigned int flags = VIR_CONNECT_GET_ALL_DOMAINS_STATS_ACTIVE | VIR_CONNECT_GET_ALL_DOMAINS_STATS_RUNNING;
		unsigned int stats = VIR_DOMAIN_STATS_VCPU;

		int numDomains = virConnectGetAllDomainStats(conn, stats, &domainStatsList, flags);
		if(numDomains == -1){
			printf("Unable to get domain statistics");	
		} else {
			//if there are no domains, do nothing
			if(numDomains == 0){
				printf("no active and running domains found");
			} else {
				domainStatsPtr currDomainStats = malloc(numDomains*sizeof(domainStats));
				for(int i=0; i<numDomains; i++){
					currDomainStats[i] = getCurrVcpuTime(domainStatsList[i]);
				}
				
				if(prevNumDomains!=0 && numDomains==prevNumDomains){//we need to compare between same list of domains
					calculateDomainUsage(currDomainStats, prevDomainStats, timeInterval, numDomains);
					
					//find how many cpu hypervisor has
					virNodeInfoPtr nodeInfo = malloc(sizeof(virNodeInfo));
					if(virNodeGetInfo(conn, nodeInfo)==-1){
						printf("Unable to get node info");
					} else {
						int numPcpus = nodeInfo->cpus;
						pcpuStatsPtr currPcpuStats = malloc(numPcpus*sizeof(pcpuStats));
						getPcpuUsage(currDomainStats, currPcpuStats, numDomains);
						
						if(prevNumPcpu!=0 && numPcpus==prevNumPcpu){ //if more pcpus were enabled, we can't compare at present
							calculatePcpuUsage(currPcpuStats, prevPcpuStats, numPcpus, timeInterval);
							
							pinVcpuToPcpu(currDomainStats, currPcpuStats, numDomains, numPcpus);
						}
						
						prevNumPcpu = numPcpus;
						prevPcpuStats = malloc(numPcpus*sizeof(pcpuStats));
						memcpy(prevPcpuStats, currPcpuStats, numPcpus * sizeof(pcpuStats));
						free(currPcpuStats);
					}
				}
				prevNumDomains = numDomains;
				prevDomainStats = malloc(numDomains*sizeof(domainStats));
				memcpy(prevDomainStats, currDomainStats,numDomains * sizeof(domainStats));
				free(currDomainStats);

			}
		}
		
		sleep(timeInterval);	
		//according to documentation, this array needs to be freed.
		virDomainStatsRecordListFree(domainStatsList);	
		
		
	}
	
	virConnectClose(conn);
	return 0;
}


