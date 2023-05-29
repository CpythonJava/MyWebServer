```
STATE_MACHINE(){
    // type_A是初始状态
    State cur_State = type_A;
    // type_C是结束状态
    while(cur_State != type_C){
        // 获取数据包
        Package _pack = getNewPackage();
        switch(){
            case type_A:
                process_pkg_state_A(_pack);
                cur_State = type_B;
                break;
            case type_B:
                process_pkg_state_B(_pack);
                cur_State = type_C;
                break;
        }
    }
}
```