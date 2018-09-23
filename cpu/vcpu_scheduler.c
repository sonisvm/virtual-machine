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
	double usage;
} pcpuStats;

typedef pcpuStats * pcpuStatsPtr;

static int calculateDomainUsage(domainStatsPtr currDomainStats, domainStatsPtr prevDomainStats, int t, int numDomains){

	for(int i=0; i<numDomains; i++){
		currDomainStats[i].usage = 100*(currDomainStats[i].time - prevDomainStats[i].time)/(t*pow(10,9));
	}
	
	return 0;
}


static void calculatePcpuUsage(domainStatsPtr currDomainStats,pcpuStatsPtr currPcpuStats, int numDomains ){
	for(int i=0; i<numDomains; i++){
		virVcpuInfoPtr info = malloc(sizeof(virVcpuInfo));
		virDomainGetVcpus(currDomainStats[i].domain, info, 1, NULL, 0);
		currPcpuStats[info->cpu].usage += currDomainStats[i].usage; 
		printf("usage for pcpu%d is %lf\n", info->cpu, currPcpuStats[info->cpu].usage);
	}
}



static domainStats getCurrVcpuTime(virDomainPtr domain){
	domainStats d;
	d.domain = domain;
	
	int nparams = virDomainGetCPUStats(domain, NULL, 0, -1, 1, 0); // nparams
	virTypedParameterPtr params = calloc(nparams, sizeof(virTypedParameter));
	virDomainGetCPUStats(domain, params, nparams, -1, 1, 0); // total stats

	//get the cpu_time
	//Assumption is that there is only one vcpu
	unsigned long long cpuTime;
	for(int i=0; i<nparams; i++){
		if(strcmp(params[i].field, "cpu_time")==0){
			cpuTime = params[i].value.ul;
			break;
		}

	}

	d.time = cpuTime ;
	d.usage = 0.0;
	return d;
}

static void pinVcpuToPcpu(domainStatsPtr currDomainStats, pcpuStatsPtr currPcpuStats, int numDomains, int numPcpus){
	double maxUsage = currPcpuStats[0].usage, minUsage = currPcpuStats[0].usage;

	int freestPcpu = 0, busiestPcpu=0;

	for(int i=1; i<numPcpus; i++){
	
		if(currPcpuStats[i].usage < minUsage){
			freestPcpu = i;
			minUsage = currPcpuStats[i].usage;
		}
		if(currPcpuStats[i].usage > maxUsage){
			busiestPcpu = i;
			maxUsage = currPcpuStats[i].usage;
		}
	}
	printf("maxUsage=%lf, minUsage=%lf\n", maxUsage, minUsage);
	if(maxUsage - minUsage <= 35.0) {
		printf("The pcpus are balanced\n");
		return;
	}	

	//find the domain with heaviest and freest load
	int busiestDomain = -1, freestDomain=-1;
	maxUsage = INT_MIN, minUsage = INT_MAX;
	for(int i=0; i<numDomains; i++){
		if(currDomainStats[i].usage > maxUsage){
			busiestDomain = i;
			maxUsage = currDomainStats[i].usage;
		}
		if(currDomainStats[i].usage < minUsage){
			freestDomain = i;
			minUsage = currDomainStats[i].usage;
		}
	}
	if(busiestDomain ==-1) return;
	

	unsigned char cpuMap = 0x1 << freestPcpu;
	printf("cpuMap=%d\n", cpuMap);fflush(stdout);

	
	if(virDomainPinVcpu(currDomainStats[busiestDomain].domain, 0, &cpuMap, (numPcpus/8)+1)==-1){
		printf("Unable to pin vcpu to pcpu");
	}

	cpuMap = 0x1 << busiestPcpu;
	printf("cpuMap=%d\n", cpuMap);fflush(stdout);
	if(virDomainPinVcpu(currDomainStats[freestDomain].domain, 0, &cpuMap, (numPcpus/8)+1)==-1){
		printf("Unable to pin vcpu to pcpu");
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
	
	domainStatsPtr prevDomainStats = NULL;
	pcpuStatsPtr prevPcpuStats = NULL;
	int prevNumDomains;
	//int prevNumPcpu ;

	//get stats for all the domains
	virDomainPtr * activeDomains = NULL;
	int numDomains = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if(numDomains == -1){
		printf("Unable to get domain statistics");
		return -1;	
	}

	while(1){

		domainStatsPtr currDomainStats = malloc(numDomains*sizeof(domainStats));
		memset(currDomainStats, 0, numDomains*sizeof(domainStats));
		for(int i=0; i<numDomains; i++){
			currDomainStats[i] = getCurrVcpuTime(activeDomains[i]);
			printf("domain=%d, cputime =%lld\n", i, currDomainStats[i].time);fflush(stdout);
		}
		
		virNodeInfoPtr nodeInfo = malloc(sizeof(virNodeInfo));
		int numPcpus = 0;
		pcpuStatsPtr currPcpuStats;
		if(virNodeGetInfo(conn, nodeInfo)==-1){
			printf("Unable to get node info");
		} else {
			numPcpus = nodeInfo->cpus;
			currPcpuStats = malloc(numPcpus*sizeof(pcpuStats));
			memset(currPcpuStats, 0, numPcpus*sizeof(pcpuStats));

		}

		if(prevDomainStats!=NULL){
			calculateDomainUsage(currDomainStats, prevDomainStats, timeInterval, numDomains);

			calculatePcpuUsage(currDomainStats, currPcpuStats, numDomains);
			pinVcpuToPcpu(currDomainStats, currPcpuStats, numDomains, numPcpus);
		}

		free(currPcpuStats);

		prevNumDomains = numDomains;
		prevDomainStats = malloc(numDomains*sizeof(domainStats));			
		memcpy(prevDomainStats, currDomainStats,numDomains * sizeof(domainStats));
		free(currDomainStats);

		sleep(timeInterval);	
		
		
	}
	for(int i=0; i<numDomains; i++){
		virDomainFree(activeDomains[i]);
	}
	free(activeDomains);
	free(prevPcpuStats);
	free(prevDomainStats);
	virConnectClose(conn);
	return 0;
}


