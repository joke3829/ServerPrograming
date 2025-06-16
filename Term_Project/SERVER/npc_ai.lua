myid = 99999;
target_id = 99999;
myState = 0;


function set_myid(x)
	myid = x;
end

function set_target(x)
	target_id = x;
end

function set_state(x)
	myState = x
end


function npcAI()
	if(myState == 0 or myState == 1) then
		myState = API_CheckUser(myid);
	elseif(myState == 2) then	
		myState = API_Roming(myid);
	else
		myState = API_Chase_target(myid, target_id);
	end
end