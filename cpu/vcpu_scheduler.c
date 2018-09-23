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

static void getPcpuUsage(domainStatsPtr currDomainStats,pcpuStatsPtr currPcpuStats, int numDomains){
	for(int i=0; i<numDomains; i++){
		virVcpuInfoPtr info = malloc(sizeof(virVcpuInfo));
		virDomainGetVcpus(currDomainStats[i].domain, info, 1, NULL, 0);
		
		currPcpuStats[info->cpu].time += currDomainStats[i].time; 
		printf("time for pcpu%d is %lld\n", info->cpu, currPcpuStats[info->cpu].time);
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
	unsigned long long cpuTime, userTime, systemTime;
	for(int i=0; i<nparams; i++){
		if(strcmp(params[i].field, "cpu_time")==0){
			cpuTime = params[i].value.ul;
			break;
		}

	}
	/*system_time + user_time is giving the overhead of QEMU / KVM on the host side. And cpu_time - (user_time + guest_time) is giving the actual amount of time the guest OS was running its CPUs.*/
	d.time = cpuTime ;
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
		printf("The pcpus are balanced\n");
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
	

	unsigned char cpuMap = 0x1 << freestPcpu;
	printf("cpuMap=%d\n", cpuMap);fflush(stdout);

	
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
	int prevNumDomains;
	int prevNumPcpu ;

	//get stats for all the domains
	virDomainPtr * activeDomains = NULL;
	int numDomains = virConnectListAllDomains(conn, &activeDomains, VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if(numDomains == -1){
		printf("Unable to get domain statistics");
		return -1;	
	}

	while(1){

		domainStatsPtr currDomainStats = malloc(numDomains*sizeof(domainStats));
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
			getPcpuUsage(currDomainStats, currPcpuStats, numDomains);
		}
		if(prevDomainStats==NULL){
			printf("prevDomainStats is null\n");fflush(stdout);
		}
		if(prevPcpuStats==NULL){
			printf("prevPcpuStats is null\n");fflush(stdout);
		}
		if(prevDomainStats!=NULL && prevPcpuStats!=NULL){
			calculateDomainUsage(currDomainStats, prevDomainStats, timeInterval, numDomains);
			calculatePcpuUsage(currPcpuStats, prevPcpuStats, numPcpus, timeInterval);
			pinVcpuToPcpu(currDomainStats, currPcpuStats, numDomains, numPcpus);
		}
		prevNumPcpu = numPcpus;
		prevPcpuStats = malloc(numPcpus*sizeof(pcpuStats));
		memcpy(prevPcpuStats, currPcpuStats, numPcpus * sizeof(pcpuStats));
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


