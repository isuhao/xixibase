## Hello World ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class HelloWorld  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		cc.set("xixi", "Hello World!");
		String value = (String)cc.get("xixi");
		System.out.println(value);
		
		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
Hello World!
```

## Add ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class Add  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		cc.add("key", "value1");
		System.out.println(cc.get("key"));
		
		cc.add("key", "value2");
		System.out.println(cc.get("key"));

		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
value1
value1
```

## Delete ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class Delete  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		cc.set("key", "value");
		System.out.println(cc.get("key"));
		
		cc.delete("key");
		System.out.println(cc.get("key"));
		
		long cacheID1 = cc.set("key", "value1");
		System.out.println(cc.get("key"));
		long cacheID2 = cc.set("key", "value2");
		System.out.println(cc.get("key"));

		cc.delete("key", cacheID1);
		System.out.println(cc.get("key"));

		cc.delete("key", cacheID2);
		System.out.println(cc.get("key"));

		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
value
null
value1
value2
value2
null
```
## Set ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class Set  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		long cacheID1 = cc.set("key", "value1");
		System.out.println(cc.get("key"));
		
		long cacheID2 = cc.set("key", "value2");
		System.out.println(cc.get("key"));

		cc.set("key", "value3", 0, cacheID1);
		System.out.println(cc.get("key"));
		
		cc.set("key", "value4", 0, cacheID2);
		System.out.println(cc.get("key"));

		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
value1
value2
value2
value4
```
## Replace ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class Replace  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		cc.replace("key", "value1");
		System.out.println(cc.get("key"));
		
		long cacheID1 = cc.add("key", "value2");
		System.out.println(cc.get("key"));
		
		long cacheID2 = cc.replace("key", "value3");
		System.out.println(cc.get("key"));

		cc.replace("key", "value4", 0, cacheID1);
		System.out.println(cc.get("key"));
		
		cc.replace("key", "value5", 0, cacheID2);
		System.out.println(cc.get("key"));

		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
null
value2
value3
value3
value5
```
## Gets ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;
import com.xixibase.cache.CacheItem;

public class Gets  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		CacheClient cc = manager.createClient();
		
		cc.set("key", "value1");
		CacheItem item1 = cc.gets("key");
		System.out.println(item1.getValue());
		
		cc.set("key", "value2");
		CacheItem item2 = cc.gets("key");
		System.out.println(item2.getValue());

		cc.set("key", "value3", 0, item1.getCacheID());
		System.out.println(cc.get("key"));
		
		cc.set("key", "value4", 0, item2.getCacheID());
		System.out.println(cc.get("key"));

		cc.flush();
		
		manager.shutdown();
	}
}
```
```
// result
value1
value2
value2
value4
```
## Group ##
```
import com.xixibase.cache.CacheClient;
import com.xixibase.cache.CacheClientManager;

public class Group  {

	public static void main(String[] args) {
		String servers = "localhost:7788";
		if (args.length >= 1) {
			servers = args[0];
		}

		String[] serverlist = servers.split(",");

		CacheClientManager manager = CacheClientManager.getInstance("example");
		manager.initialize(serverlist);
		
		int groupID1 = 1;
		int groupID2 = 2;
		
		CacheClient cc1 = manager.createClient(groupID1);
		CacheClient cc2 = manager.createClient(groupID2);
		
		cc1.set("key", "value1");
		cc2.set("key", "value2");
		
		System.out.println(cc1.get("key"));
		System.out.println(cc2.get("key"));
		
		cc1.delete("key");
		
		System.out.println(cc1.get("key"));
		System.out.println(cc2.get("key"));
		
		cc1.set("key", "value1B");
		cc2.set("key", "value2B");
		
		cc2.flush();
		
		System.out.println(cc1.get("key"));
		System.out.println(cc2.get("key"));
		
		cc1.flush();
		cc2.flush();
		
		manager.shutdown();
	}
}
```
```
// result
value1
value2
null
value2
value1B
null
```