/*
 * asclite
 * Author: Jerome Ajot
 *
 * This software was developed at the National Institute of Standards and Technology by
 * employees of the Federal Government in the course of their official duties.  Pursuant to
 * Title 17 Section 105 of the United States Code this software is not subject to copyright
 * protection within the United States and is in the public domain. asclite is
 * an experimental system.  NIST assumes no responsibility whatsoever for its use by any party.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS."  With regard to this software, NIST MAKES NO EXPRESS
 * OR IMPLIED WARRANTY AS TO ANY MATTER WHATSOEVER, INCLUDING MERCHANTABILITY,
 * OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "uemfilter.h"

Logger* UEMElement::m_pLogger = Logger::getLogger();
Logger* UEMFilter::m_pLogger = Logger::getLogger();

UEMElement::UEMElement(string _file, string _channel, int _startTime, int _endTime)
{
	m_File = _file;
	m_Channel = _channel;
	m_StartTime = _startTime;
	m_EndTime = _endTime;
}

UEMFilter::~UEMFilter()
{
	for(size_t i=0; i<m_VectUEMElements.size(); ++i)
		if(m_VectUEMElements[i])
			delete m_VectUEMElements[i];
	
	m_VectUEMElements.clear();
}

UEMElement* UEMFilter::FindElement(string file, string channel)
{
	for(size_t i=0; i<m_VectUEMElements.size(); ++i)
		if(m_VectUEMElements[i])
		{
			/*
			if( (m_VectUEMElements[i]->GetFile() == file) && (m_VectUEMElements[i]->GetChannel() == channel) )
				return m_VectUEMElements[i];
			*/
			
			if( (file.find(m_VectUEMElements[i]->GetFile(), 0) != 0) && (channel.compare(m_VectUEMElements[i]->GetChannel()) == 0) )
				return m_VectUEMElements[i];
		}

	return NULL;
}

int UEMFilter::ParseString(string chaine)
{
	if(strchr(chaine.c_str(),'.') == NULL)
	{
		return(atoi(chaine.c_str()) * 1000);
	}
	else
	{
		int bf, af;
		char c2[BUFFER_SIZE];
		sscanf(chaine.c_str(), "%d.%s", (int*) &bf, (char*) &c2);
		
		int len = strlen(c2);
		
		af = atoi(c2);
				
		if(len > 3)
		{
			af = (int)((double)(af)/pow(10, len-3));
		}
		else
		{
			af = (int)((double)(af)*pow(10, 3-len));
		}
		
		return( bf*1000+af);		
	}
}

void UEMFilter::LoadFile(string filename)
{
	ifstream file;
	string line;
	file.open(filename.c_str(), ifstream::in);
	
	long int lineNum = -1;
    
	if (! file.is_open())
	{ 
		LOG_FATAL(m_pLogger, "Error opening file " + filename); 
		exit (1); 
	}
	
	char l_file[BUFFER_SIZE];
	char l_channel[BUFFER_SIZE];
	char l_start[BUFFER_SIZE];
	char l_end[BUFFER_SIZE];
	
	while (getline(file,line,'\n'))
	{
		 ++lineNum;
	
		if (line.find_first_of(";;") == 0)
		{
			//comment so skip (for now)
		}
		else
		{
			int nbArgParsed = 0;
			nbArgParsed = sscanf(line.c_str(), "%s %s %s %s", (char*) &l_file, (char*) &l_channel, (char*) &l_start, (char*) &l_end);
			 
			if(nbArgParsed != 4)
			{
				char buffer[BUFFER_SIZE];
				sprintf(buffer, "Error parsing the line %li in file %s", lineNum, filename.c_str());
				LOG_ERR(m_pLogger, buffer);
			}
			else
			{
				int start_ms = ParseString(string(l_start));
				int end_ms = ParseString(string(l_end));
				
				if(start_ms < end_ms)
				{
					UEMElement* pUEMElement = new UEMElement(string(l_file), string(l_channel), start_ms, end_ms);
					AddUEMElement(pUEMElement);
				}
				else
				{
					char buffer[BUFFER_SIZE];
					sprintf(buffer, "The time is not proper at the line %li in file %s: begin time %s and endtime %s", lineNum, filename.c_str(), l_start, l_end);
					LOG_ERR(m_pLogger, buffer);
				}
			}
		}
	}
    
	LOG_INFO(m_pLogger, "loading of file '" + filename + "' done");
	file.close();
    
	if(isEmpty())
	{
		LOG_FATAL(m_pLogger, "UEM file '" + filename + "' contains no data!");
		exit(1);
	}
	
	m_bUseFile = true;
}

bool UEMFilter::HasInterSegmentGaps(Speech* speech)
{
	for(size_t i=0; i<speech->NbOfSegments(); ++i)
	{
		if(speech->GetSegment(i)->GetSpeakerId().compare(string("inter_segment_gap")) == 0)
			return true;
	}
	
	return false;
}

unsigned long int UEMFilter::ProcessSingleSpeech(Speech* speech)
{
	if(HasInterSegmentGaps(speech))
	{
		LOG_INFO(m_pLogger, "UEMFilter: Inter Segment Gap detected on the input - Abording filtering");
		return 0;
	}
	
	ulint nbrerr = 0;
	list<Segment*> listSegmentsToRemove;
	
	// Step 1: checking if the input is proper and listing segments to remove
	if(speech->GetParentSpeechSet()->IsRef())
	{
		// It's a Ref so check the bad ones
		for(size_t segindex=0; segindex<speech->NbOfSegments(); ++segindex)
		{
			Segment* pSegment = speech->GetSegment(segindex);
		
			string segFile = pSegment->GetSource();
			string segChannel = pSegment->GetChannel();
			
			UEMElement* pUEMElement = FindElement(segFile, segChannel);
			
			if(pUEMElement == NULL)
			{
				listSegmentsToRemove.push_back(pSegment);
			}
			else
			{
				int segStartTime = pSegment->GetStartTime();
				int segEndTime = pSegment->GetEndTime();
				int uemStartTime = pUEMElement->GetStartTime();
				int uemEndTime = pUEMElement->GetEndTime();
				
				if( (uemEndTime < segStartTime) ||
					(segEndTime < uemStartTime) )
				{
					listSegmentsToRemove.push_back(pSegment);
				}
				else
				{
					++nbrerr;
					LOG_ERR(m_pLogger, "UEMFilter: " + segFile + "/" + segChannel + " has un improper time regarding the uem");
				}
			}
		}
	}
	else
	{
		// It's a Hyp so just remove them regarding the mid point
	}
	
	// Step 2: removing the unwanted segments
	list<Segment*>::iterator  i = listSegmentsToRemove.begin();
	list<Segment*>::iterator ei = listSegmentsToRemove.end();
	
	while(i != ei)
	{
		speech->RemoveSegment(*i);
		++i;
	}
	
	listSegmentsToRemove.clear();
	
	// Step 3: adding the ISG
	
	
	return nbrerr;
}

unsigned long int UEMFilter::ProcessSpeechSet(SpeechSet* pSpeechSet)
{
	return 0;
}

