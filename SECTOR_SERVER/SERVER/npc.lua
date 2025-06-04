myid = 99999;
player_id = 99999;

direction = 0;
cnt = 0;
run = false;

function set_uid(x)
   myid = x;
end

function event_npc_move()
    API_move_dir(myid, direction, run);
    if(run == true) then
        cnt = cnt + 1;
        if(cnt > 3) then
            run = false;
            API_SendMessage(myid, player_id, "BYE");
        end
    end
end

function event_player_move(player)
    if(run == false) then
        player_x = API_get_x(player);
        player_y = API_get_y(player);
        my_x = API_get_x(myid);
        my_y = API_get_y(myid);
        if (player_x == my_x) then
            if (player_y == my_y) then
                API_SendMessage(myid, player, "HELLO");
                run = true;
                direction = math.random(0, 3);
                cnt = 0;
                player_id = player;
            end
        end
    end
end