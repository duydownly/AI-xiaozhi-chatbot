import React from "react";
import { NavigationContainer } from "@react-navigation/native";
import { createNativeStackNavigator } from "@react-navigation/native-stack";

import ConnectPage from "./src/connectpage";   // trang connect
import HomePage from "./src/homepage";         // trang home
import ActionScreen from "./src/page/actionscreen";
const Stack = createNativeStackNavigator();

export default function App() {
  return (
    <NavigationContainer>
      <Stack.Navigator initialRouteName="Connect">
        
        <Stack.Screen 
          name="Connect" 
          component={ConnectPage} 
          options={{ title: "Kết nối Robot" }}
        />

        <Stack.Screen 
          name="Home" 
          component={HomePage} 
          options={{ title: "Trang Chủ" }}
        />
        <Stack.Screen
          name="ActionScreen"
          component={ActionScreen}
          options={{title:"Trang Điều khiển"}}
        />
      </Stack.Navigator>
    </NavigationContainer>
  );
}
