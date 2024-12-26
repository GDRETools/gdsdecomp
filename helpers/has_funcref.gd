extends Object

func foo():
	return ("bar")

func test():
	var a = funcref(self, "foo")
	return 100