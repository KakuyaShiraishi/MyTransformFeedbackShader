#pragma once

#include "ofMain.h"

class TransformFeedbackShader : public ofShader
{
public:
	
	struct AttributeDescription {
		string front_name;
		string back_name;
		int elemCount;
	};
	
	bool setup(const string& vertex_shader_path,
			   const vector<AttributeDescription>& varyings,
			   int count)
	{
		ping_pong_map.clear();
		
		ping_pong_index = 0;
		
		this->vertex_shader_path = vertex_shader_path;
		this->transform_feedback_varyings = varyings;
		this->count = count;
		
		ofShader::setupShaderFromFile(GL_VERTEX_SHADER, vertex_shader_path);
		linkProgramTransformFeedback();
		
		init_buffers();
	}
	
	void reload(bool recall_initial_data = false)
	{
		ofShader::unload();
		ofShader::setupShaderFromFile(GL_VERTEX_SHADER, vertex_shader_path);
		linkProgramTransformFeedback();
		
		if (recall_initial_data)
		{
			map<string, PingPong>::iterator it = ping_pong_map.begin();
			while (it != ping_pong_map.end())
			{
				if (it->second.initial_data.size())
				{
					recallInitialData(it->first);
				}
				it++;
			}
		}
	}
	
	void updateTransformFeedback()
	{
		glEnable(GL_RASTERIZER_DISCARD);
		
		vbo.bind();
		{
			for (int i = 0; i < transform_feedback_varyings.size(); i++)
			{
				const string& front_name = transform_feedback_varyings[i].front_name;
				
				int loc = getAttributeLocation(front_name);
				glEnableVertexAttribArray(loc);
				
				PingPong& ping_pong = ping_pong_map[front_name];
				ping_pong.buffer[ping_pong_index].bind(GL_ARRAY_BUFFER);
				
				glVertexAttribPointer(loc, ping_pong.elemCount, GL_FLOAT, GL_FALSE, 0, (void*)0);
				
				glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, i, ping_pong.buffer[1 - ping_pong_index].getId());
			}
			
			glBeginTransformFeedback(GL_POINTS);
			glDrawArrays(GL_POINTS, 0, count);
			glEndTransformFeedback();
			
			for (int i = 0; i < transform_feedback_varyings.size(); i++)
			{
				const string& front_name = transform_feedback_varyings[i].front_name;
				PingPong& ping_pong = ping_pong_map[front_name];
				
				ping_pong.buffer[ping_pong_index].unbind(GL_ARRAY_BUFFER);
				
				int loc = getAttributeLocation(front_name);
				glDisableVertexAttribArray(loc);
			}
		}
		vbo.unbind();
		
		glDisable(GL_RASTERIZER_DISCARD);
		
		ping_pong_index++;
		ping_pong_index %= 2;
	}
	
	void attach(const string& src_name,
				ofShader& target_shader,
				ofVbo& target_vbo,
				const string& target_name,
				int divisor = 0)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(src_name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid src name";
			return;
		}
		
		int loc = target_shader.getAttributeLocation(target_name);
		if (loc == -1)
		{
			ofLogError("TransformFeedbackShader") << "invalid target name";
			return;
		}
		
		PingPong& ping_pong = it->second;
		ofBufferObject& buf = ping_pong.buffer[ping_pong_index];
		
		target_vbo.bind();
		target_vbo.setAttributeBuffer(loc, buf, ping_pong.elemCount, 0);
		target_vbo.setAttributeDivisor(loc, divisor);
		target_vbo.unbind();
	}

	ofBufferObject& getBufferObject(const string& name)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid name";
			static ofBufferObject temp;
			return temp;
		}
		
		return it->second.buffer[ping_pong_index];
	}
	
	template <typename T>
	bool getData(const string& name, std::vector<T>& data)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid name";
			return false;
		}
		
		PingPong& ping_pong = it->second;
		if (ping_pong.elemCount != (sizeof(T) / sizeof(float)))
		{
			ofLogError("TransformFeedbackShader") << "invalid element size";
			return false;
		}
		
		ofBufferObject& buf = it->second.buffer[ping_pong_index];
		
		T* p = buf.map<T>(GL_READ_ONLY);
		if (p)
		{
			int N = buf.size() / sizeof(T);
			data.assign(p, p + N);
			buf.unmap();
		}
		
		return true;
	}
	
	template <typename T>
	bool setData(const string& name, const std::vector<T>& data)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid name";
			return false;
		}
		
		if (data.size() != count)
		{
			ofLogError("TransformFeedbackShader") << "invalid size";
			return false;
		}

		PingPong& ping_pong = it->second;
		if (ping_pong.elemCount != (sizeof(T) / sizeof(float)))
		{
			ofLogError("TransformFeedbackShader") << "invalid element size";
			return false;
		}
		
		ping_pong.buffer[0].setData(data, GL_STATIC_DRAW);
		ping_pong.buffer[1].setData(data, GL_STATIC_DRAW);
		
		return true;
	}

	template <typename T>
	bool setInitialData(const string& name, const std::vector<T>& data)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid name";
			return false;
		}
		
		if (data.size() != count)
		{
			ofLogError("TransformFeedbackShader") << "invalid size";
			return false;
		}
		
		PingPong& ping_pong = it->second;
		if (ping_pong.elemCount != (sizeof(T) / sizeof(float)))
		{
			ofLogError("TransformFeedbackShader") << "invalid element size";
			return false;
		}
		
		ping_pong.initial_data.resize(ping_pong.elemCount * data.size());
		memcpy(ping_pong.initial_data.data(), data.data(), ping_pong.initial_data.size());
		
		ping_pong.buffer[0].setData(ping_pong.initial_data, GL_STATIC_DRAW);
		ping_pong.buffer[1].setData(ping_pong.initial_data, GL_STATIC_DRAW);

		return true;
	}
	
	bool recallInitialData(const string& name)
	{
		map<string, PingPong>::iterator it = ping_pong_map.find(name);
		if (it == ping_pong_map.end())
		{
			ofLogError("TransformFeedbackShader") << "invalid name";
			return false;
		}
		
		PingPong& ping_pong = it->second;
		ping_pong.buffer[0].setData(ping_pong.initial_data, GL_STATIC_DRAW);
		ping_pong.buffer[1].setData(ping_pong.initial_data, GL_STATIC_DRAW);

		return true;
	}
	
	size_t getSize() const { return count; }
	
	
protected:
	
	struct PingPong {
		ofBufferObject buffer[2];
		int elemCount;
		vector<float> initial_data;
	};
	
	string vertex_shader_path;
	vector<AttributeDescription> transform_feedback_varyings;
	
	int count;
	
	int ping_pong_index;
	map<string, PingPong> ping_pong_map;
	
	ofVbo vbo;
	
protected:
	
	bool linkProgramTransformFeedback() {
		if(shaders.empty()) {
			ofLogError("ofShader") << "linkProgram(): trying to link GLSL program, but no shaders created yet";
		} else {
			checkAndCreateProgram();
			/*
            for(unordered_map<GLenum, GLuint>::const_iterator it = shaders.begin(); it != shaders.end(); ++it){
				GLuint shader = it->second;
				if(shader) {
					ofLogVerbose("ofShader") << "linkProgram(): attaching " << nameForType(it->first) << " shader to program " << program;
					glAttachShader(program, shader);
				}
			}*/
            for(auto it: shaders){
                auto shader = it.second;
                if(shader.id > 0) {
                    ofLogVerbose("ofShader") << "linkProgram(): attaching " << nameForType(it.first) << " shader to program " << program;
                    glAttachShader(program, shader.id);
                }
            }
            
            
			if (transform_feedback_varyings.size())
			{
				const char** varyings = new const char*[transform_feedback_varyings.size()];
				
				for (int i = 0; i < transform_feedback_varyings.size(); i++)
				{
					varyings[i] = transform_feedback_varyings[i].back_name.c_str();
				}
				
				glTransformFeedbackVaryings(program, transform_feedback_varyings.size(), varyings, GL_SEPARATE_ATTRIBS);
				
				delete [] varyings;
			}
			
			
			glLinkProgram(program);
			
			checkProgramLinkStatus(program);
			
			// bLoaded means we have loaded shaders onto the graphics card;
			// it doesn't necessarily mean that these shaders have compiled and linked successfully.
			bLoaded = true;
		}
		return bLoaded;
	}
	
	void init_buffers()
	{
		vbo.bind();
		
		for (int i = 0; i < transform_feedback_varyings.size(); i++)
		{
			const string& front_name = transform_feedback_varyings[i].front_name;
			int loc = getAttributeLocation(front_name);
			PingPong& ping_pong = ping_pong_map[front_name];
			ping_pong.elemCount = transform_feedback_varyings[i].elemCount;
			
			assert(ping_pong.elemCount > 0);
			
			if (ping_pong.buffer[0].isAllocated() == false)
			{
				ping_pong.buffer[0].bind(GL_ARRAY_BUFFER);
				ping_pong.buffer[0].allocate(sizeof(float) * ping_pong.elemCount * count, GL_STATIC_DRAW);
				ping_pong.buffer[0].unbind(GL_ARRAY_BUFFER);
			}
			
			if (ping_pong.buffer[1].isAllocated() == false)
			{
				ping_pong.buffer[1].bind(GL_ARRAY_BUFFER);
				ping_pong.buffer[1].allocate(sizeof(float) * ping_pong.elemCount * count, GL_STATIC_DRAW);
				ping_pong.buffer[1].unbind(GL_ARRAY_BUFFER);
			}
		}
		
		vbo.unbind();
	}
	
private:
	
	bool load(string shaderName);
	bool load(string vertName, string fragName, string geomName="");
	
	bool setupShaderFromSource(GLenum type, string source, string sourceDirectoryPath = "");
	bool setupShaderFromFile(GLenum type, string filename);
	bool linkProgram();
};
