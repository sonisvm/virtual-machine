## Instructions to compile
> gcc -g -Wall -o memory_coordinator memory_coordinator.c -lvirt.

OR

> Run the command `make`.

## Code Description
### Problem statement
Given a set of virtual machines having different memory usage, coordinate the memory among the virtual domains so that there is no wastage of memory.

### Memory coordination policy
The policy used for memory coordination is as below:
- Two thresholds are used
  - `FREE_THRESHOLD` - amount of unused memory above which a domain is considered to be wasting memory.
  - `LOADED_THRESHOLD` - amount of unused memory below which a domain is considered to be 
- Find the domain D1 which has the least unused memory and domain D2 which has the most unused memory.
- If D1 has less than `LOADED_THRESHOLD` of unused memory
  - If D2 has more than `FREE_THRESHOLD` of unused memory
    - `LOADED_THRESHOLD` amount of memory is taken away from D2 and given to D1.
  - If not, `FREE_THRESHOLD` amount of memory is given to D1 from host.
- If D2 has more than `FREE_THRESHOLD` of unused memory
  - `LOADED_THRESHOLD` amount of memory is reclaimed.
- If host has less than LOADED_THRESHOLD of unused memory
  - `LOADED_THRESHOLD` amount of memory is reclaimed from all domains which have more than `FREE_THRESHOLD` of unused memory.

### Code description
#### Structures used
1. `memStats` - contains following information
	1. `domain` - pointer to the domain, type - `virDomainPtr`.
	2. `unused` - amount of unused memory.
	3. `available` - amount of memory available for the domain.
  
#### Algorithm
1. Connect to hypervisor using `virConnectOpen()`.
2. Get the list of all active and running domains using `virConnectListAllDomains()`.
3. Set the memory collection period for each domain using `virDomainSetMemoryStatsPeriod()`.
4. Create an array of `memStatsPtr` called `memStats` to hold the statistics information of each domain.
5. Invoke `updateMemStats()` to get the current memory statistics.
	1. `updateMemStats` makes use of `virDomainMemoryStats()` to get the statistics.
	2. The tag `VIR_DOMAIN_MEMORY_STAT_UNUSED` and `VIR_DOMAIN_MEMORY_STAT_AVAILABLE` are extracted.
6. Find the domain D1 with least unused memory and D2 with most unused memory.
7. If D1 has less than or equal to `LOADED_THRESHOLD` amount of unused memory
	1. If D2 has more than or equal to `FREE_THRESHOLD` amount of unused memory
		1. Set the memory of D1 to `available memory` + `LOADED_THRESHOLD` using `virDomainSetMemory()`.
		2. Set the memory of D2 to `available memory` - `LOADED_THRESHOLD` using `virDomainSetMemory()`.
	2. Else, Set the memory of D1 to `available memory` + `FREE_THRESHOLD` using `virDomainSetMemory()`.
8. Else, if D2 has more than or equal to `FREE_THRESHOLD` amount of unused memory
	1. Set the memory of D2 to `available memory` - `LOADED_THRESHOLD` using `virDomainSetMemory()`.
9. If the host has less than `LOADED_THRESHOLD` amount of unused memory
	1. Go through all domains which have more than `FREE_THRESHOLD` amount of unused memory and set the memory to `available memory` - `LOADED_THRESHOLD`.
10. Sleep for the specified time interval `t` and then start again from step 4.
