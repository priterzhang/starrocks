// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Limited.
package com.starrocks.proc;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;

import com.starrocks.common.proc.FrontendsProcNode;
import com.starrocks.ha.FrontendNodeType;
import com.starrocks.system.Frontend;

import org.junit.Assert;
import org.junit.Test;


import mockit.Expectations;
import mockit.Mocked;

public class FrontendsProcNodeTest {

    @Mocked
    InetSocketAddress socketAddr1;
    @Mocked
    InetAddress addr1;

    private void mockAddress() {
        new Expectations(){
            {
                socketAddr1.getAddress();
                result = addr1;
            }
        };
        new Expectations(){
            {
                socketAddr1.getPort();
                result = 1000;
            }
        };
        new Expectations(){
            {
                addr1.getHostAddress();
                result = "127.0.0.1";
            }
        };
        new Expectations(){
            {
                addr1.getHostName();
                result = "sandbox";
            }
        };
    }

    @Test    
    public void testIsJoin() throws ClassNotFoundException, 
                                    NoSuchMethodException, 
                                    SecurityException, 
                                    IllegalAccessException, 
                                    IllegalArgumentException, 
                                    InvocationTargetException {
        mockAddress();
        List<InetSocketAddress> list = new ArrayList<InetSocketAddress>();
        list.add(socketAddr1);
        
        Class<?> clazz = Class.forName(FrontendsProcNode.class.getName());
        Method isJoin = clazz.getDeclaredMethod("isJoin", new Class[]{List.class, Frontend.class});
        isJoin.setAccessible(true);

        Frontend feCouldFoundByIP = new Frontend(FrontendNodeType.MASTER,"test","127.0.0.1",1000);
        boolean result1 = (boolean) isJoin.invoke(FrontendsProcNode.class, list, feCouldFoundByIP);
        Assert.assertTrue(result1);

        Frontend feCouldNotFoundByIP = new Frontend(FrontendNodeType.MASTER,"test","127.0.0.2",1000);
        boolean result2 = (boolean) isJoin.invoke(FrontendsProcNode.class, list, feCouldNotFoundByIP);
        Assert.assertTrue(!result2);

        Frontend feCouldFoundByHostName = new Frontend(FrontendNodeType.MASTER,"test","sandbox",1000);
        boolean result3 = (boolean) isJoin.invoke(FrontendsProcNode.class, list, feCouldFoundByHostName);
        Assert.assertTrue(result3);

        Frontend feCouldNotFoundByHostName = new Frontend(FrontendNodeType.MASTER,"test","sandbox1",1000);
        boolean result4 = (boolean) isJoin.invoke(FrontendsProcNode.class, list, feCouldNotFoundByHostName);
        Assert.assertTrue(!result4);
    }

    @Test    
    public void testIPTitle() {
        Assert.assertTrue(FrontendsProcNode.TITLE_NAMES.get(1).equals("IP"));
    }
}
